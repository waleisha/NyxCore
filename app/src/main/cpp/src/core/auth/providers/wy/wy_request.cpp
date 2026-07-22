#include "src/core/auth/providers/wy/wy_request.h"

#include "src/core/auth/auth_session.h"
#include "src/core/auth/providers/wy/wy_codec.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

// 表单参数 URL 编码
std::string url_encode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// 追加一个表单参数
void append_param(std::string& out, const std::string& key, const std::string& value) {
    if (!out.empty()) {
        out.push_back('&');
    }
    out.append(key);
    out.push_back('=');
    out.append(url_encode(value));
}

// WY nonce 使用当前时间后 6 位，不足补零
std::string pad6(std::int64_t value) {
    if (value < 0) {
        value = -value;
    }
    std::string text = std::to_string(value % 1000000);
    if (text.size() < 6) {
        text.insert(text.begin(), 6 - text.size(), '0');
    }
    return text;
}

// 生成 WY 请求签名，字段顺序必须和服务端规则保持一致
std::string make_sign(
    const Profile& profile,
    const std::string& license,
    const std::string& device_id,
    const std::string& timestamp,
    const std::string& extra_key,
    const std::string& extra_value
) {
    std::string raw;
    raw.reserve(
        profile.fields.license.size() +
        profile.fields.device.size() +
        profile.fields.time.size() +
        profile.app_key.size() +
        license.size() +
        device_id.size() +
        timestamp.size() +
        extra_key.size() +
        extra_value.size() +
        16
    );
    raw.append(profile.fields.license).append("=").append(license)
        .append("&").append(profile.fields.device).append("=").append(device_id)
        .append("&").append(profile.fields.time).append("=").append(timestamp);
    if (!extra_key.empty()) {
        raw.append("&").append(extra_key).append("=").append(extra_value);
    }
    raw.append("&").append(profile.app_key);
    return md5_hex(raw);
}

// 初始化通用请求路径
Request build_request(const Profile& profile) {
    Request req;
    req.path = endpoint_path(profile);
    return req;
}

} // namespace

// 构造登录请求
Request make_login_request(const Profile& profile, const LoginInput& input, std::int64_t now_seconds) {
    const Profile next = with_defaults(profile);
    Request req = build_request(next);
    req.timestamp = now_seconds;
    req.nonce = pad6(now_seconds);
    req.sign = make_sign(next, input.license, input.device_id, std::to_string(req.timestamp), "", "");
    append_param(req.body, "id", next.calls.login.id);
    append_param(req.body, next.fields.license, input.license);
    append_param(req.body, next.fields.device, input.device_id);
    append_param(req.body, next.fields.time, std::to_string(req.timestamp));
    append_param(req.body, next.fields.sign, req.sign);
    append_param(req.body, next.fields.nonce, req.nonce);
    return req;
}

// 构造心跳请求
Request make_heartbeat_request(const Profile& profile, const SessionInput& input, std::int64_t now_seconds) {
    const Profile next = with_defaults(profile);
    Request req = build_request(next);
    req.timestamp = now_seconds;
    req.nonce = pad6(now_seconds);
    req.sign = make_sign(next, input.license, input.device_id, std::to_string(req.timestamp), next.fields.token, input.token);
    append_param(req.body, "id", next.calls.heartbeat.id);
    append_param(req.body, next.fields.license, input.license);
    append_param(req.body, next.fields.device, input.device_id);
    append_param(req.body, next.fields.time, std::to_string(req.timestamp));
    append_param(req.body, next.fields.sign, req.sign);
    append_param(req.body, next.fields.token, input.token);
    append_param(req.body, next.fields.nonce, req.nonce);
    return req;
}

// 构造远程变量请求
Request make_var_request(const Profile& profile, const VarInput& input, std::int64_t now_seconds) {
    const Profile next = with_defaults(profile);
    Request req = build_request(next);
    req.timestamp = now_seconds;
    req.nonce = pad6(now_seconds);
    const std::string param_name = next.fields.var_key;
    req.sign = make_sign(next, input.license, input.device_id, std::to_string(req.timestamp), param_name, input.key);
    append_param(req.body, "id", next.calls.var.id);
    append_param(req.body, next.fields.license, input.license);
    append_param(req.body, next.fields.device, input.device_id);
    append_param(req.body, next.fields.time, std::to_string(req.timestamp));
    append_param(req.body, next.fields.sign, req.sign);
    append_param(req.body, next.fields.nonce, req.nonce);
    append_param(req.body, param_name, input.key);
    return req;
}

// 构造公告请求
Request make_notice_request(const Profile& profile) {
    const Profile next = with_defaults(profile);
    Request req = build_request(next);
    const std::string call_id = next.calls.notice ? next.calls.notice->id : next.calls.login.id;
    append_param(req.body, "id", call_id);
    return req;
}

// 构造更新检查请求
Request make_update_request(const Profile& profile) {
    const Profile next = with_defaults(profile);
    Request req = build_request(next);
    const std::string call_id = next.calls.update ? next.calls.update->id : next.calls.login.id;
    append_param(req.body, "id", call_id);
    return req;
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
