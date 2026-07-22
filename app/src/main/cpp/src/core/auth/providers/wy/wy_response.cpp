#include "src/core/auth/providers/wy/wy_response.h"

#include "src/utils/json_utils.h"

#include "nlohmann/json.hpp"

#include <cctype>
#include <cstdint>
#include <ctime>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

using Json = nlohmann::json;

namespace json = ::nyx::utils::json;

// 将 JSON 数值或数字字符串转为 int64
bool json_to_int64(const Json& value, std::int64_t& out) {
    try {
        if (value.is_number_integer() || value.is_number_unsigned()) {
            out = value.get<std::int64_t>();
            return true;
        }
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            std::size_t consumed = 0;
            const std::int64_t number = std::stoll(text, &consumed, 10);
            if (consumed != text.size()) {
                return false;
            }
            out = number;
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

// 将 JSON 标量转为字符串，对象和数组保留 JSON 文本
std::string json_to_string(const Json& value) {
    try {
        if (value.is_string()) {
            return value.get<std::string>();
        }
        if (value.is_number_integer() || value.is_number_unsigned()) {
            return std::to_string(value.get<std::int64_t>());
        }
        if (value.is_number_float()) {
            return std::to_string(value.get<double>());
        }
        if (value.is_boolean()) {
            return value.get<bool>() ? "true" : "false";
        }
        if (value.is_null()) {
            return {};
        }
        return value.dump();
    } catch (...) {
        return {};
    }
}

// 将 JSON 标量转为字符串，对象和数组视为不可用
std::string json_to_scalar_string(const Json& value) {
    if (value.is_object() || value.is_array()) {
        return {};
    }
    return json_to_string(value);
}

// 从多个候选字段中提取第一个非空文本
std::string extract_text(const Json& obj, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            const std::string text = json_to_scalar_string(*it);
            if (!text.empty()) {
                return text;
            }
        }
    }
    return {};
}

// 从多个候选字段中提取第一个 int64
std::int64_t extract_int64(const Json& obj, std::initializer_list<const char*> keys, bool* found = nullptr) {
    for (const auto* key : keys) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            std::int64_t out = 0;
            if (json_to_int64(*it, out)) {
                if (found != nullptr) {
                    *found = true;
                }
                return out;
            }
        }
    }
    if (found != nullptr) {
        *found = false;
    }
    return 0;
}

// 从多个候选字段中提取第一个 int
int extract_int(const Json& obj, std::initializer_list<const char*> keys, bool* found = nullptr) {
    bool local_found = false;
    const std::int64_t value = extract_int64(obj, keys, &local_found);
    if (!local_found ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        if (found != nullptr) {
            *found = false;
        }
        return 0;
    }

    if (found != nullptr) {
        *found = true;
    }
    return static_cast<int>(value);
}

// 解析功能授权票据列表
std::vector<FeatureTicket> extract_features(const Json& obj) {
    std::vector<FeatureTicket> tickets;
    const auto* items = json::read_array(obj, "features");
    if (items == nullptr) {
        return tickets;
    }

    for (const auto& item : *items) {
        std::string feature;
        bool allowed = false;
        std::int64_t expires_at = 0;
        if (!item.is_object()) {
            continue;
        }
        if (!json::read_string(item, "feature", &feature) ||
            !json::read_bool(item, "allowed", &allowed) ||
            !json::read_int64(item, "expires_at", &expires_at)) {
            continue;
        }

        FeatureTicket ticket;
        ticket.feature = std::move(feature);
        ticket.allowed = allowed;
        ticket.expires_at = expires_at;
        if (!ticket.feature.empty()) {
            tickets.push_back(std::move(ticket));
        }
    }

    return tickets;
}

// 剩余次数的宽松范围判断
bool looks_like_remaining(std::int64_t value) {
    return value >= 0 && value <= 100000;
}

// 判断字符串是否只包含字母数字
bool is_alnum(const std::string& value) {
    for (char c : value) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// 判断字符串是否只包含十六进制字符
bool is_hex(const std::string& value) {
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

// WY token 通常是 5 位字母数字
bool looks_like_token(const std::string& value) {
    return value.size() == 5 && is_alnum(value);
}

// check 字段通常是 MD5 或 SHA-256 十六进制
bool looks_like_hash(const std::string& value) {
    return (value.size() == 32 || value.size() == 64) && is_hex(value);
}

// WY 成功码通常是五位业务码
bool looks_like_code(std::int64_t value) {
    return value >= 10000 && value <= 99999;
}

// 合理的 Unix 时间范围
bool looks_like_epoch(std::int64_t value) {
    return value >= 946684800 && value <= 4102444800;
}

// 旧格式里可能把字段名当作值出现，避免误判为载荷
bool is_status_key(const std::string& value) {
    return value == "code" || value == "single";
}

// 判断字段名是否像 token
bool is_token_key(const std::string& key) {
    return key == "token" || key == "kamitoken" || key == "session_token";
}

// 判断字段名是否像 check
bool is_check_key(const std::string& key) {
    return key == "check" || key == "check2";
}

// 判断字段名是否像消息文本
bool is_message_key(const std::string& key) {
    return key == "message" || key == "msg" || key == "app_gg" ||
        key == "updateshow" || key == "notice";
}

// 判断字段名是否像业务载荷
bool is_value_key(const std::string& key) {
    return key == "value" || key == "data" || key == "content" ||
        key == "remote_var" || key == "payload_seed" || key == "seed";
}

// 判断字段名是否明确是数值字段
bool is_numeric_key(const std::string& key) {
    return key == "code" || key == "single" || key == "time" ||
        key == "server_time" || key == "remaining_count" ||
        key == "remaining_uses" || key == "vip_expire" ||
        key == "expires_at" || key == "expire_at";
}

// 从旧响应里的裸数字推断业务含义
void inspect_legacy_number(std::int64_t value, ParsedResponse* out) {
    if (out == nullptr) {
        return;
    }

    if (out->code == 0 && looks_like_code(value)) {
        out->code = static_cast<int>(value);
        return;
    }

    if (!out->has_time && looks_like_epoch(value)) {
        out->server_time = value;
        out->has_time = true;
        return;
    }

    if (out->expires_at <= 0 && looks_like_epoch(value)) {
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        const std::int64_t base = out->has_time ? out->server_time : now;
        if (value > base + 60) {
            out->expires_at = value;
            return;
        }
    }

    if (out->remaining_uses <= 0 && looks_like_remaining(value)) {
        out->remaining_uses = static_cast<int>(value);
    }
}

// 从旧响应里的字符串字段推断业务含义
void inspect_legacy_string(
    const std::string& key,
    const std::string& value,
    ParsedResponse* out,
    std::vector<std::string>* hashes
) {
    if (out == nullptr || value.empty()) {
        return;
    }

    if (is_token_key(key)) {
        if (out->token.empty()) {
            out->token = value;
        }
        return;
    }

    if (is_check_key(key)) {
        if (!out->has_check) {
            out->check = value;
            out->has_check = true;
        }
        return;
    }

    if (is_message_key(key)) {
        if (out->message.empty()) {
            out->message = value;
        }
        return;
    }

    if (is_value_key(key)) {
        if (out->value.empty()) {
            out->value = value;
        }
        return;
    }

    if (is_numeric_key(key)) {
        return;
    }

    if (out->token.empty() && looks_like_token(value)) {
        out->token = value;
        return;
    }

    if (looks_like_hash(value)) {
        if (hashes != nullptr) {
            hashes->push_back(value);
        }
        return;
    }

    if (is_status_key(value)) {
        return;
    }

    if (out->value.empty()) {
        out->value = value;
    }
    if (out->message.empty() && value.size() >= 20) {
        out->message = value;
    }
}

// 递归检查旧格式响应，兼容字段名不固定的服务端返回
void inspect_legacy_object(const Json& obj, ParsedResponse* out) {
    if (out == nullptr || !obj.is_object()) {
        return;
    }

    std::vector<std::string> hashes;
    for (const auto& item : obj.items()) {
        const auto& value = item.value();
        std::int64_t number = 0;
        if (json_to_int64(value, number)) {
            if (!is_numeric_key(item.key())) {
                inspect_legacy_number(number, out);
            }
            continue;
        }
        if (value.is_string()) {
            inspect_legacy_string(item.key(), value.get<std::string>(), out, &hashes);
        } else {
            const std::string text = json_to_scalar_string(value);
            if (!text.empty()) {
                inspect_legacy_string(item.key(), text, out, &hashes);
            }
        }
    }

    if (!out->has_check && !hashes.empty()) {
        out->check = hashes.front();
        out->has_check = true;
    }

    for (const auto& item : obj.items()) {
        if (item.value().is_object()) {
            inspect_legacy_object(item.value(), out);
        }
    }
}

// 解析标准状态字段
void parse_status_fields(const Json& obj, ParsedResponse* out) {
    if (out == nullptr || !obj.is_object()) {
        return;
    }

    bool found = false;
    const int code = extract_int(obj, {"code"}, &found);
    if (found && out->code == 0) {
        out->code = code;
    }

    const std::int64_t time_value = extract_int64(obj, {"time", "server_time"}, &found);
    if (found && !out->has_time) {
        out->server_time = time_value;
        out->has_time = true;
    }

    auto check_it = obj.find("check");
    if (!out->has_check && check_it != obj.end()) {
        out->check = json_to_scalar_string(*check_it);
        out->has_check = !out->check.empty();
    }
    auto check2_it = obj.find("check2");
    if (!out->has_check && check2_it != obj.end()) {
        out->check = json_to_scalar_string(*check2_it);
        out->has_check = !out->check.empty();
    }
}

// 解析标准载荷字段
void parse_payload_fields(const Json& obj, ParsedResponse* out) {
    if (out == nullptr || !obj.is_object()) {
        return;
    }

    bool found = false;
    if (out->token.empty()) {
        out->token = extract_text(obj, {"token", "kamitoken", "session_token"});
    }
    if (out->value.empty()) {
        out->value = extract_text(obj, {"value", "data", "content", "remote_var", "payload_seed", "seed"});
    }
    if (out->message.empty()) {
        out->message = extract_text(obj, {"message", "app_gg", "updateshow", "notice"});
        if (out->message.empty()) {
            auto msg = obj.find("msg");
            if (msg != obj.end()) {
                out->message = json_to_scalar_string(*msg);
            }
        }
    }

    if (!out->has_check) {
        auto check_it = obj.find("check");
        if (check_it != obj.end()) {
            out->check = json_to_scalar_string(*check_it);
            out->has_check = !out->check.empty();
        }
        auto check2_it = obj.find("check2");
        if (!out->has_check && check2_it != obj.end()) {
            out->check = json_to_scalar_string(*check2_it);
            out->has_check = !out->check.empty();
        }
    }

    const int remaining = extract_int(obj, {"remaining_count", "remaining_uses"}, &found);
    if (found && out->remaining_uses <= 0 && looks_like_remaining(remaining)) {
        out->remaining_uses = remaining;
    }

    const std::int64_t expires_at = extract_int64(obj, {"vip_expire", "expires_at", "expire_at"}, &found);
    if (found && out->expires_at <= 0) {
        out->expires_at = expires_at;
    }

    if (out->features.empty()) {
        out->features = extract_features(obj);
    }
}

// 优先解析明确命名的字段和常见嵌套对象
void parse_explicit_fields(const Json& root, ParsedResponse* out) {
    if (out == nullptr || !root.is_object()) {
        return;
    }

    parse_status_fields(root, out);
    parse_payload_fields(root, out);

    for (const char* key : {"msg", "data", "result", "payload", "content"}) {
        const Json* value = json::field(root, key);
        if (value != nullptr && value->is_object()) {
            parse_payload_fields(*value, out);
        }
    }
}

// 用旧格式推断结果补齐标准字段没解析到的部分
void merge_missing(ParsedResponse* out, const ParsedResponse& fallback) {
    if (out == nullptr) {
        return;
    }

    if (out->code == 0) {
        out->code = fallback.code;
    }
    if (!out->has_time && fallback.has_time) {
        out->server_time = fallback.server_time;
        out->has_time = true;
    }
    if (!out->has_check && fallback.has_check) {
        out->check = fallback.check;
        out->has_check = true;
    }
    if (out->message.empty()) {
        out->message = fallback.message;
    }
    if (out->token.empty()) {
        out->token = fallback.token;
    }
    if (out->value.empty()) {
        out->value = fallback.value;
    }
    if (out->remaining_uses <= 0) {
        out->remaining_uses = fallback.remaining_uses;
    }
    if (out->expires_at <= 0) {
        out->expires_at = fallback.expires_at;
    }
    if (out->features.empty()) {
        out->features = fallback.features;
    }
}

} // namespace

// 解析 WY JSON 响应
bool parse(const std::string& body, ParsedResponse* out) {
    if (out == nullptr) {
        return false;
    }

    *out = ParsedResponse{};
    if (body.empty()) {
        return false;
    }

    try {
        const Json root = Json::parse(body);
        if (!root.is_object()) {
            return false;
        }

        out->valid = true;
        parse_explicit_fields(root, out);

        ParsedResponse legacy;
        legacy.valid = true;
        inspect_legacy_object(root, &legacy);
        merge_missing(out, legacy);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
