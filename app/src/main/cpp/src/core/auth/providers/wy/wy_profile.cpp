#include "src/core/auth/providers/wy/wy_profile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

// 默认 WY 域名
constexpr const char* kDefaultHost = "wy.llua.cn";
// 默认 WY 路径前缀
constexpr const char* kDefaultPathPrefix = "/v2/";

// 编译期 WY 配置默认值，可由构建参数覆盖
#ifndef NYX_AUTH_WY_HOST
#define NYX_AUTH_WY_HOST ""
#endif
#ifndef NYX_AUTH_WY_PATH_PREFIX
#define NYX_AUTH_WY_PATH_PREFIX ""
#endif
#ifndef NYX_AUTH_WY_API_TOKEN
#define NYX_AUTH_WY_API_TOKEN ""
#endif
#ifndef NYX_AUTH_WY_APP_KEY
#define NYX_AUTH_WY_APP_KEY ""
#endif
#ifndef NYX_AUTH_WY_LOGIN_CALL_ID
#define NYX_AUTH_WY_LOGIN_CALL_ID ""
#endif
#ifndef NYX_AUTH_WY_LOGIN_SUCCESS_CODE
#define NYX_AUTH_WY_LOGIN_SUCCESS_CODE 0
#endif
#ifndef NYX_AUTH_WY_HEARTBEAT_CALL_ID
#define NYX_AUTH_WY_HEARTBEAT_CALL_ID ""
#endif
#ifndef NYX_AUTH_WY_HEARTBEAT_SUCCESS_CODE
#define NYX_AUTH_WY_HEARTBEAT_SUCCESS_CODE 0
#endif
#ifndef NYX_AUTH_WY_VAR_CALL_ID
#define NYX_AUTH_WY_VAR_CALL_ID ""
#endif
#ifndef NYX_AUTH_WY_VAR_SUCCESS_CODE
#define NYX_AUTH_WY_VAR_SUCCESS_CODE 0
#endif
#ifndef NYX_AUTH_WY_NOTICE_CALL_ID
#define NYX_AUTH_WY_NOTICE_CALL_ID ""
#endif
#ifndef NYX_AUTH_WY_NOTICE_SUCCESS_CODE
#define NYX_AUTH_WY_NOTICE_SUCCESS_CODE 0
#endif
#ifndef NYX_AUTH_WY_UPDATE_CALL_ID
#define NYX_AUTH_WY_UPDATE_CALL_ID ""
#endif
#ifndef NYX_AUTH_WY_UPDATE_SUCCESS_CODE
#define NYX_AUTH_WY_UPDATE_SUCCESS_CODE 0
#endif
#ifndef NYX_AUTH_WY_LOGIN_CHECK_KIND
#define NYX_AUTH_WY_LOGIN_CHECK_KIND 0
#endif
#ifndef NYX_AUTH_WY_HEARTBEAT_CHECK_KIND
#define NYX_AUTH_WY_HEARTBEAT_CHECK_KIND 2
#endif
#ifndef NYX_AUTH_WY_VAR_CHECK_KIND
#define NYX_AUTH_WY_VAR_CHECK_KIND 2
#endif
#ifndef NYX_AUTH_WY_SALT
#define NYX_AUTH_WY_SALT ""
#endif
#ifndef NYX_AUTH_WY_RC4_KEY
#define NYX_AUTH_WY_RC4_KEY ""
#endif
#ifndef NYX_AUTH_WY_ALPHABET
#define NYX_AUTH_WY_ALPHABET ""
#endif
#ifndef NYX_AUTH_WY_LICENSE_FIELD
#define NYX_AUTH_WY_LICENSE_FIELD "kami"
#endif
#ifndef NYX_AUTH_WY_DEVICE_FIELD
#define NYX_AUTH_WY_DEVICE_FIELD "markcode"
#endif
#ifndef NYX_AUTH_WY_TOKEN_FIELD
#define NYX_AUTH_WY_TOKEN_FIELD "kamitoken"
#endif
#ifndef NYX_AUTH_WY_TIME_FIELD
#define NYX_AUTH_WY_TIME_FIELD "t"
#endif
#ifndef NYX_AUTH_WY_SIGN_FIELD
#define NYX_AUTH_WY_SIGN_FIELD "sign"
#endif
#ifndef NYX_AUTH_WY_NONCE_FIELD
#define NYX_AUTH_WY_NONCE_FIELD "value"
#endif
#ifndef NYX_AUTH_WY_VAR_FIELD
#define NYX_AUTH_WY_VAR_FIELD "key"
#endif
#ifndef NYX_AUTH_WY_VARS
#define NYX_AUTH_WY_VARS ""
#endif

// 读取环境变量，缺失时使用编译期默认值
std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') {
        return value;
    }
    return fallback != nullptr ? fallback : "";
}

// 读取整数环境变量，解析失败时使用默认值
int env_or_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

// 读取 check 类型环境变量
CheckKind env_or_check(const char* name, CheckKind fallback) {
    return static_cast<CheckKind>(env_or_int(name, static_cast<int>(fallback)));
}

// 去掉字符串首尾空白
std::string trim(std::string value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

// 添加一条变量别名映射，格式为 alias=remote_key
void add_var(std::unordered_map<std::string, std::string>* vars, std::string entry) {
    const std::size_t split = entry.find('=');
    if (split == std::string::npos) {
        return;
    }

    std::string alias = trim(entry.substr(0, split));
    std::string remote_key = trim(entry.substr(split + 1));
    if (alias.empty() || remote_key.empty()) {
        return;
    }

    (*vars)[std::move(alias)] = std::move(remote_key);
}

// 解析变量映射列表，条目用 | 分隔
void load_vars(const std::string& value, std::unordered_map<std::string, std::string>* vars) {
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t split = value.find('|', start);
        const std::size_t end = split == std::string::npos ? value.size() : split;
        add_var(vars, value.substr(start, end - start));
        if (split == std::string::npos) {
            break;
        }
        start = split + 1;
    }
}

// 判断编解码步骤是否包含指定步骤
bool has_step(const std::vector<CodecStep>& steps, CodecStep want) {
    for (const auto step : steps) {
        if (step == want) {
            return true;
        }
    }
    return false;
}

// 判断 check 类型是否受支持
bool valid_check_kind(CheckKind kind) {
    switch (kind) {
        case CheckKind::TimeAppKeyNonceSalt:
        case CheckKind::AppKeyNonceSalt:
        case CheckKind::TimeAppKeyNonce:
            return true;
    }
    return false;
}

// 请求和响应编解码必须包含 RC4、Hex 和 DefBase
bool valid_codec_steps(const std::vector<CodecStep>& steps) {
    return !steps.empty() &&
        has_step(steps, CodecStep::Rc4) &&
        has_step(steps, CodecStep::Hex) &&
        has_step(steps, CodecStep::DefBase);
}

// 校验自定义 Base64 字母表
bool valid_alphabet(const std::string& alphabet) {
    if (alphabet.size() < 64) {
        return false;
    }

    std::array<bool, 256> seen{};
    for (std::size_t i = 0; i < 64; ++i) {
        const auto c = static_cast<unsigned char>(alphabet[i]);
        if (alphabet[i] == '=' || seen[c]) {
            return false;
        }
        seen[c] = true;
    }
    return true;
}

// 生成运行时异或 key，混入计数、盐和栈地址
std::uint8_t runtime_key(std::uint32_t salt) {
    static std::atomic<std::uint32_t> counter{0x7f4a7c15U};
    const auto count = counter.fetch_add(0x45d9f3bU, std::memory_order_relaxed);
    const auto addr = reinterpret_cast<std::uintptr_t>(&salt);
    std::uint32_t mixed = count ^ salt ^ static_cast<std::uint32_t>(addr);
    mixed ^= static_cast<std::uint32_t>(addr >> 8);
    mixed ^= mixed >> 16;
    mixed *= 0x45d9f3bU;
    const auto key = static_cast<std::uint8_t>((mixed ^ (mixed >> 8)) & 0xffU);
    return key == 0 ? 0xa5 : key;
}

// 擦除明文字符串内容
void wipe(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

// 将明文文本存成 RuntimeText
RuntimeText store_text(std::string value, std::uint32_t salt) {
    RuntimeText out;
    out.key = runtime_key(salt + static_cast<std::uint32_t>(value.size()));
    out.bytes.reserve(value.size());
    for (const unsigned char c : value) {
        out.bytes.push_back(static_cast<std::uint8_t>(c ^ out.key));
    }
    wipe(value);
    return out;
}

// 打开 RuntimeText 得到明文文本
std::string open_text(const RuntimeText& text) {
    std::string out;
    out.reserve(text.bytes.size());
    for (const std::uint8_t value : text.bytes) {
        out.push_back(static_cast<char>(value ^ text.key));
    }
    return out;
}

// 存储调用配置
RuntimeCall store_call(Call call, std::uint32_t salt) {
    RuntimeCall out;
    out.id = store_text(std::move(call.id), salt);
    out.success_code = call.success_code;
    return out;
}

// 打开调用配置
Call open_call(const RuntimeCall& call) {
    return Call{open_text(call.id), call.success_code};
}

} // namespace

// 补齐默认 host、字段名、编解码步骤和成功码
Profile with_defaults(Profile profile) {
    if (profile.endpoint.host.empty()) {
        profile.endpoint.host = kDefaultHost;
    }
    if (profile.endpoint.path_prefix.empty()) {
        profile.endpoint.path_prefix = kDefaultPathPrefix;
    }
    if (profile.fields.license.empty()) {
        profile.fields.license = "kami";
    }
    if (profile.fields.device.empty()) {
        profile.fields.device = "markcode";
    }
    if (profile.fields.token.empty()) {
        profile.fields.token = "kamitoken";
    }
    if (profile.fields.time.empty()) {
        profile.fields.time = "t";
    }
    if (profile.fields.sign.empty()) {
        profile.fields.sign = "sign";
    }
    if (profile.fields.nonce.empty()) {
        profile.fields.nonce = "value";
    }
    if (profile.fields.var_key.empty()) {
        profile.fields.var_key = "key";
    }
    if (profile.codec.request_steps.empty()) {
        profile.codec.request_steps = {
            CodecStep::Rc4,
            CodecStep::Hex,
            CodecStep::DefBase,
            CodecStep::Hex,
        };
    }
    if (profile.codec.response_steps.empty()) {
        profile.codec.response_steps = {
            CodecStep::Hex,
            CodecStep::DefBase,
            CodecStep::Hex,
            CodecStep::Rc4,
        };
    }
    if (profile.calls.login.success_code <= 0) {
        profile.calls.login.success_code = 78563;
    }
    if (profile.calls.heartbeat.success_code <= 0) {
        profile.calls.heartbeat.success_code = 29646;
    }
    if (profile.calls.var.success_code <= 0) {
        profile.calls.var.success_code = 33404;
    }
    return profile;
}

// 判断 profile 是否足够发起 WY 请求
bool is_configured(const Profile& profile) {
    const Profile next = with_defaults(profile);
    return !next.endpoint.host.empty() &&
        !next.endpoint.api_token.empty() &&
        !next.app_key.empty() &&
        !next.calls.login.id.empty() &&
        next.calls.login.success_code > 0 &&
        !next.calls.heartbeat.id.empty() &&
        next.calls.heartbeat.success_code > 0 &&
        !next.calls.var.id.empty() &&
        next.calls.var.success_code > 0 &&
        !next.check.salt.empty() &&
        !next.codec.rc4_key.empty() &&
        valid_alphabet(next.codec.alphabet) &&
        valid_codec_steps(next.codec.request_steps) &&
        valid_codec_steps(next.codec.response_steps) &&
        valid_check_kind(next.check.login) &&
        valid_check_kind(next.check.heartbeat) &&
        valid_check_kind(next.check.var) &&
        next.check.max_time_skew_seconds >= 0;
}

// 生成请求路径
std::string endpoint_path(const Profile& profile) {
    const Profile next = with_defaults(profile);
    return next.endpoint.path_prefix + next.endpoint.api_token;
}

// 将本地变量别名解析成 WY 远端变量 key
std::string resolve_var(const Profile& profile, const std::string& key) {
    const auto it = profile.vars.find(key);
    if (it == profile.vars.end() || it->second.empty()) {
        return {};
    }
    return it->second;
}

// 将明文配置转换为运行时配置
RuntimeProfile store_profile(Profile profile) {
    profile = with_defaults(std::move(profile));

    RuntimeProfile out;
    out.endpoint.host = store_text(std::move(profile.endpoint.host), 0x1001);
    out.endpoint.path_prefix = store_text(std::move(profile.endpoint.path_prefix), 0x1002);
    out.endpoint.api_token = store_text(std::move(profile.endpoint.api_token), 0x1003);
    out.app_key = store_text(std::move(profile.app_key), 0x1004);

    out.calls.login = store_call(std::move(profile.calls.login), 0x2001);
    out.calls.heartbeat = store_call(std::move(profile.calls.heartbeat), 0x2002);
    out.calls.var = store_call(std::move(profile.calls.var), 0x2003);
    if (profile.calls.notice) {
        out.calls.notice = store_call(std::move(*profile.calls.notice), 0x2004);
    }
    if (profile.calls.update) {
        out.calls.update = store_call(std::move(*profile.calls.update), 0x2005);
    }

    out.fields.license = store_text(std::move(profile.fields.license), 0x3001);
    out.fields.device = store_text(std::move(profile.fields.device), 0x3002);
    out.fields.token = store_text(std::move(profile.fields.token), 0x3003);
    out.fields.time = store_text(std::move(profile.fields.time), 0x3004);
    out.fields.sign = store_text(std::move(profile.fields.sign), 0x3005);
    out.fields.nonce = store_text(std::move(profile.fields.nonce), 0x3006);
    out.fields.var_key = store_text(std::move(profile.fields.var_key), 0x3007);

    out.codec.rc4_key = store_text(std::move(profile.codec.rc4_key), 0x4001);
    out.codec.alphabet = store_text(std::move(profile.codec.alphabet), 0x4002);
    out.codec.request_steps = std::move(profile.codec.request_steps);
    out.codec.response_steps = std::move(profile.codec.response_steps);

    out.check.login = profile.check.login;
    out.check.heartbeat = profile.check.heartbeat;
    out.check.var = profile.check.var;
    out.check.salt = store_text(std::move(profile.check.salt), 0x5001);
    out.check.max_time_skew_seconds = profile.check.max_time_skew_seconds;

    out.vars.reserve(profile.vars.size());
    for (auto& entry : profile.vars) {
        out.vars.emplace_back(
            store_text(std::move(entry.first), 0x6001),
            store_text(std::move(entry.second), 0x6002)
        );
    }

    return out;
}

// 将运行时配置还原成明文配置
Profile open_profile(const RuntimeProfile& profile) {
    Profile out;
    out.endpoint.host = open_text(profile.endpoint.host);
    out.endpoint.path_prefix = open_text(profile.endpoint.path_prefix);
    out.endpoint.api_token = open_text(profile.endpoint.api_token);
    out.app_key = open_text(profile.app_key);

    out.calls.login = open_call(profile.calls.login);
    out.calls.heartbeat = open_call(profile.calls.heartbeat);
    out.calls.var = open_call(profile.calls.var);
    if (profile.calls.notice) {
        out.calls.notice = open_call(*profile.calls.notice);
    }
    if (profile.calls.update) {
        out.calls.update = open_call(*profile.calls.update);
    }

    out.fields.license = open_text(profile.fields.license);
    out.fields.device = open_text(profile.fields.device);
    out.fields.token = open_text(profile.fields.token);
    out.fields.time = open_text(profile.fields.time);
    out.fields.sign = open_text(profile.fields.sign);
    out.fields.nonce = open_text(profile.fields.nonce);
    out.fields.var_key = open_text(profile.fields.var_key);

    out.codec.rc4_key = open_text(profile.codec.rc4_key);
    out.codec.alphabet = open_text(profile.codec.alphabet);
    out.codec.request_steps = profile.codec.request_steps;
    out.codec.response_steps = profile.codec.response_steps;

    out.check.login = profile.check.login;
    out.check.heartbeat = profile.check.heartbeat;
    out.check.var = profile.check.var;
    out.check.salt = open_text(profile.check.salt);
    out.check.max_time_skew_seconds = profile.check.max_time_skew_seconds;

    for (const auto& entry : profile.vars) {
        const std::string alias = open_text(entry.first);
        const std::string remote_key = open_text(entry.second);
        if (!alias.empty() && !remote_key.empty()) {
            out.vars[alias] = remote_key;
        }
    }

    return with_defaults(std::move(out));
}

// 从编译宏或环境变量加载 WY 配置
Profile load_profile() {
    Profile profile;
    profile.endpoint.host = env_or("NYX_AUTH_WY_HOST", NYX_AUTH_WY_HOST);
    profile.endpoint.path_prefix = env_or("NYX_AUTH_WY_PATH_PREFIX", NYX_AUTH_WY_PATH_PREFIX);
    profile.endpoint.api_token = env_or("NYX_AUTH_WY_API_TOKEN", NYX_AUTH_WY_API_TOKEN);
    profile.app_key = env_or("NYX_AUTH_WY_APP_KEY", NYX_AUTH_WY_APP_KEY);

    profile.calls.login.id = env_or("NYX_AUTH_WY_LOGIN_CALL_ID", NYX_AUTH_WY_LOGIN_CALL_ID);
    profile.calls.login.success_code = env_or_int("NYX_AUTH_WY_LOGIN_SUCCESS_CODE", NYX_AUTH_WY_LOGIN_SUCCESS_CODE);
    profile.calls.heartbeat.id = env_or("NYX_AUTH_WY_HEARTBEAT_CALL_ID", NYX_AUTH_WY_HEARTBEAT_CALL_ID);
    profile.calls.heartbeat.success_code = env_or_int(
        "NYX_AUTH_WY_HEARTBEAT_SUCCESS_CODE",
        NYX_AUTH_WY_HEARTBEAT_SUCCESS_CODE
    );
    profile.calls.var.id = env_or("NYX_AUTH_WY_VAR_CALL_ID", NYX_AUTH_WY_VAR_CALL_ID);
    profile.calls.var.success_code = env_or_int("NYX_AUTH_WY_VAR_SUCCESS_CODE", NYX_AUTH_WY_VAR_SUCCESS_CODE);

    const std::string notice_id = env_or("NYX_AUTH_WY_NOTICE_CALL_ID", NYX_AUTH_WY_NOTICE_CALL_ID);
    const int notice_code = env_or_int("NYX_AUTH_WY_NOTICE_SUCCESS_CODE", NYX_AUTH_WY_NOTICE_SUCCESS_CODE);
    if (!notice_id.empty() && notice_code > 0) {
        profile.calls.notice = Call{notice_id, notice_code};
    }

    const std::string update_id = env_or("NYX_AUTH_WY_UPDATE_CALL_ID", NYX_AUTH_WY_UPDATE_CALL_ID);
    const int update_code = env_or_int("NYX_AUTH_WY_UPDATE_SUCCESS_CODE", NYX_AUTH_WY_UPDATE_SUCCESS_CODE);
    if (!update_id.empty() && update_code > 0) {
        profile.calls.update = Call{update_id, update_code};
    }

    profile.check.login = env_or_check(
        "NYX_AUTH_WY_LOGIN_CHECK_KIND",
        static_cast<CheckKind>(NYX_AUTH_WY_LOGIN_CHECK_KIND)
    );
    profile.check.heartbeat = env_or_check(
        "NYX_AUTH_WY_HEARTBEAT_CHECK_KIND",
        static_cast<CheckKind>(NYX_AUTH_WY_HEARTBEAT_CHECK_KIND)
    );
    profile.check.var = env_or_check("NYX_AUTH_WY_VAR_CHECK_KIND", static_cast<CheckKind>(NYX_AUTH_WY_VAR_CHECK_KIND));
    profile.check.salt = env_or("NYX_AUTH_WY_SALT", NYX_AUTH_WY_SALT);

    profile.fields.license = env_or("NYX_AUTH_WY_LICENSE_FIELD", NYX_AUTH_WY_LICENSE_FIELD);
    profile.fields.device = env_or("NYX_AUTH_WY_DEVICE_FIELD", NYX_AUTH_WY_DEVICE_FIELD);
    profile.fields.token = env_or("NYX_AUTH_WY_TOKEN_FIELD", NYX_AUTH_WY_TOKEN_FIELD);
    profile.fields.time = env_or("NYX_AUTH_WY_TIME_FIELD", NYX_AUTH_WY_TIME_FIELD);
    profile.fields.sign = env_or("NYX_AUTH_WY_SIGN_FIELD", NYX_AUTH_WY_SIGN_FIELD);
    profile.fields.nonce = env_or("NYX_AUTH_WY_NONCE_FIELD", NYX_AUTH_WY_NONCE_FIELD);
    profile.fields.var_key = env_or("NYX_AUTH_WY_VAR_FIELD", NYX_AUTH_WY_VAR_FIELD);

    profile.codec.rc4_key = env_or("NYX_AUTH_WY_RC4_KEY", NYX_AUTH_WY_RC4_KEY);
    profile.codec.alphabet = env_or("NYX_AUTH_WY_ALPHABET", NYX_AUTH_WY_ALPHABET);

    load_vars(env_or("NYX_AUTH_WY_VARS", NYX_AUTH_WY_VARS), &profile.vars);

    return with_defaults(std::move(profile));
}

// 测试用样例配置
Profile sample_profile() {
    Profile profile;
    profile.endpoint.host = "wy.example.invalid";
    profile.endpoint.path_prefix = "/api/";
    profile.endpoint.api_token = "wy-token";
    profile.app_key = "wy-app-key";

    profile.calls.login = Call{"login", 78563};
    profile.calls.heartbeat = Call{"heartbeat", 29646};
    profile.calls.var = Call{"var", 33404};
    profile.calls.notice = Call{"notice", 37303};
    profile.calls.update = Call{"update", 37405};

    profile.fields.license = "kami";
    profile.fields.device = "markcode";
    profile.fields.token = "kamitoken";
    profile.fields.time = "t";
    profile.fields.sign = "sign";
    profile.fields.nonce = "value";
    profile.fields.var_key = "key";

    profile.codec.rc4_key = "wy-rc4-key";
    profile.codec.alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    profile.check.login = CheckKind::TimeAppKeyNonceSalt;
    profile.check.heartbeat = CheckKind::TimeAppKeyNonce;
    profile.check.var = CheckKind::TimeAppKeyNonce;
    profile.check.salt = "wy-salt";
    profile.check.max_time_skew_seconds = 30;

    profile.vars["payload_seed"] = "remote_payload_seed";
    profile.vars["feature_flag"] = "remote_feature_flag";
    return with_defaults(std::move(profile));
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
