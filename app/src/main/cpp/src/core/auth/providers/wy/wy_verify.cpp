#include "src/core/auth/providers/wy/wy_verify.h"

#include "src/core/auth/providers/wy/wy_codec.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

// WY 域名使用的 CA 证书
constexpr char kCertumTrustedNetworkCa[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDuzCCAqOgAwIBAgIDBETAMA0GCSqGSIb3DQEBBQUAMH4xCzAJBgNVBAYTAlBM\n"
    "MSIwIAYDVQQKExlVbml6ZXRvIFRlY2hub2xvZ2llcyBTLkEuMScwJQYDVQQLEx5D\n"
    "ZXJ0dW0gQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkxIjAgBgNVBAMTGUNlcnR1bSBU\n"
    "cnVzdGVkIE5ldHdvcmsgQ0EwHhcNMDgxMDIyMTIwNzM3WhcNMjkxMjMxMTIwNzM3\n"
    "WjB+MQswCQYDVQQGEwJQTDEiMCAGA1UEChMZVW5pemV0byBUZWNobm9sb2dpZXMg\n"
    "Uy5BLjEnMCUGA1UECxMeQ2VydHVtIENlcnRpZmljYXRpb24gQXV0aG9yaXR5MSIw\n"
    "IAYDVQQDExlDZXJ0dW0gVHJ1c3RlZCBOZXR3b3JrIENBMIIBIjANBgkqhkiG9w0B\n"
    "AQEFAAOCAQ8AMIIBCgKCAQEA4/t9o3K6wvDJFIf1awFO4W5AB7ptJ11/91sts1rH\n"
    "UV+rpDKmYYe2bg+G0jACl/jXaVehGDldamR5xgFZrDwxSjh80gTSSyjoIF87B6LM\n"
    "TXPb865Px1bVWqeWifrzq2jUI4ZZJ88JJ7ysbnKDHDBy3+Ci6dLhdHUZvSqeexVU\n"
    "BBvXQzmtVSjF4hq79MDkrjhJM8x2hZ85RdKknvISjFH4fOQtf/WsX+sWn7Et0brM\n"
    "kUJ3TCXJkDhv2/DM+44el1k+1WBO5gUo7Ul5E0u6SNsv+XLTOcr+H9g0cvW0QM8x\n"
    "AcPs3hEtF10fuFDRXhmnad4HMyjKUJX5p1TLVIZQRan5SQIDAQABo0IwQDAPBgNV\n"
    "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBQIds3LB/8k9sXN7buQvOKEN0Z19zAOBgNV\n"
    "HQ8BAf8EBAMCAQYwDQYJKoZIhvcNAQEFBQADggEBAKaorSLOAT2mo/9i0Eidi15y\n"
    "sHhE49wcrwn9I0j6vSrEuVUEtRCjjSfeC4Jj0O7eDDd5QVsisrCaQVymcODU0HfL\n"
    "I9MA4GxWL+FpDQ3Zqr8hgVDZBqWo/5U30Kr+4rP1mS1FhIrlQgnXdAIv94nYmem8\n"
    "J9RHjboNRhx3zxSkHLmkMcScKHQDNP8zGSal6Q10tz6XxnboJ5ajZt3hrvJBW8qY\n"
    "VoNzcOSGGtIxQbovvi0TWnZvTuhOgQ4/WwMioBK+ZlgRSssDxLQqKi2WF+A5VLxI\n"
    "03YnnZotBqbJ7DnSq9ufmgsnAjUpsUCV5/nonFWIGUbWtzT1fs45mtk48VH3Tyw=\n"
    "-----END CERTIFICATE-----\n";

// 常量时间字符串比较，避免 check 校验暴露早停位置
bool secure_equals(const std::string& left, const std::string& right) {
    const std::size_t len = std::max(left.size(), right.size());
    std::size_t diff = left.size() ^ right.size();
    for (std::size_t i = 0; i < len; ++i) {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        diff |= static_cast<std::size_t>(a ^ b);
    }
    return diff == 0;
}

// 根据 WY check 类型生成期望 check
std::string make_check(
    const Profile& profile,
    CheckKind kind,
    const Request& request,
    std::int64_t server_time
) {
    const std::string stamp = std::to_string(server_time > 0 ? server_time : request.timestamp);
    const std::string nonce = request.nonce;
    std::string raw;
    switch (kind) {
        case CheckKind::TimeAppKeyNonceSalt:
            raw = stamp + profile.app_key + nonce + profile.check.salt;
            return sha256_hex(md5_hex(raw));
        case CheckKind::AppKeyNonceSalt:
            raw = profile.app_key + nonce + profile.check.salt;
            return sha256_hex(md5_hex(raw));
        case CheckKind::TimeAppKeyNonce:
            raw = stamp + profile.app_key + nonce;
            return md5_hex(raw);
    }
    return {};
}

// 校验响应 check，必要时同时校验服务端时间漂移
bool verify_check(
    const Profile& profile,
    CheckKind kind,
    const Request& request,
    const ParsedResponse& response,
    bool require_time
) {
    if (!response.has_check) {
        return false;
    }

    if (require_time) {
        if (!response.has_time || request.timestamp <= 0) {
            return false;
        }
        const std::int64_t diff = response.server_time - request.timestamp;
        if (std::llabs(diff) > profile.check.max_time_skew_seconds) {
            return false;
        }
    }

    const std::string expected = make_check(profile, kind, request, response.server_time);
    return !expected.empty() && secure_equals(response.check, expected);
}

// 统一完成响应校验和 ProviderResult 转换
ProviderResult finish(
    const Profile& profile,
    const Request& request,
    const ParsedResponse& response,
    bool mismatch,
    int expected_code,
    const char* default_message,
    bool require_time
) {
    ProviderResult result;
    result.code = response.code;
    result.server_time = response.server_time;

    if (!response.valid) {
        result.failure = sdk::auth::Err::Protocol;
        result.message = "invalid response";
        return result;
    }

    if (response.code != expected_code) {
        result.failure = sdk::auth::Err::Rejected;
        result.message = response.message.empty() ? default_message : response.message;
        return result;
    }

    if (require_time) {
        if (!response.has_time || request.timestamp <= 0) {
            result.failure = sdk::auth::Err::Protocol;
            result.message = "response is missing server time";
            return result;
        }

        const std::int64_t diff = response.server_time - request.timestamp;
        if (std::llabs(diff) > profile.check.max_time_skew_seconds) {
            result.failure = sdk::auth::Err::Protocol;
            result.message = "response time drift";
            return result;
        }
    }

    if (mismatch) {
        result.failure = sdk::auth::Err::Protocol;
        result.message = "response check mismatch";
        return result;
    }

    result.success = true;
    result.failure = sdk::auth::Err::None;
    result.message = response.message.empty() ? "ok" : response.message;
    if (!response.token.empty()) {
        result.token = response.token;
    }
    if (response.has_check) {
        result.check = response.check;
        result.has_check = true;
    }
    if (response.remaining_uses > 0) {
        result.remaining_uses = response.remaining_uses;
    }
    if (!response.features.empty()) {
        result.features = response.features;
    }
    if (!response.value.empty()) {
        result.value = response.value;
    }
    if (response.expires_at > 0) {
        result.expires_at = response.expires_at;
    }
    return result;
}

} // namespace

// 校验登录响应 check
bool verify_login(const Profile& profile, const Request& request, const ParsedResponse& response) {
    const bool require_time = profile.check.login != CheckKind::AppKeyNonceSalt;
    return verify_check(profile, profile.check.login, request, response, require_time);
}

// 校验心跳响应 check
bool verify_heartbeat(const Profile& profile, const Request& request, const ParsedResponse& response) {
    return verify_check(profile, profile.check.heartbeat, request, response, true);
}

// 校验远程变量响应 check
bool verify_var(const Profile& profile, const Request& request, const ParsedResponse& response) {
    return verify_check(profile, profile.check.var, request, response, true);
}

// 生成登录 ProviderResult
ProviderResult make_login_result(const Profile& profile, const Request& request, const ParsedResponse& response) {
    const bool require_time = profile.check.login != CheckKind::AppKeyNonceSalt;
    return finish(
        profile,
        request,
        response,
        !verify_login(profile, request, response),
        profile.calls.login.success_code,
        "wy login rejected",
        require_time
    );
}

// 生成心跳 ProviderResult
ProviderResult make_heartbeat_result(const Profile& profile, const Request& request, const ParsedResponse& response) {
    return finish(
        profile,
        request,
        response,
        !verify_heartbeat(profile, request, response),
        profile.calls.heartbeat.success_code,
        "wy heartbeat rejected",
        true
    );
}

// 生成远程变量 ProviderResult
ProviderResult make_var_result(const Profile& profile, const Request& request, const ParsedResponse& response) {
    return finish(
        profile,
        request,
        response,
        !verify_var(profile, request, response),
        profile.calls.var.success_code,
        "wy variable rejected",
        true
    );
}

// 生成公告 ProviderResult
ProviderResult make_notice_result(const Profile& profile, const Request& request, const ParsedResponse& response) {
    const int code = profile.calls.notice ? profile.calls.notice->success_code : 0;
    return finish(
        profile,
        request,
        response,
        false,
        code,
        "wy notice rejected",
        false
    );
}

// 生成更新检查 ProviderResult
ProviderResult make_update_result(const Profile& profile, const Request& request, const ParsedResponse& response) {
    const int code = profile.calls.update ? profile.calls.update->success_code : 0;
    return finish(
        profile,
        request,
        response,
        false,
        code,
        "wy update rejected",
        false
    );
}

// 获取 WY 请求使用的 CA 证书
sdk::net::CaBundle trust_ca() {
    return {kCertumTrustedNetworkCa, sizeof(kCertumTrustedNetworkCa) - 1};
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
