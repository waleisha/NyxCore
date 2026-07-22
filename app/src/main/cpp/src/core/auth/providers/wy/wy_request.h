#pragma once

#include "src/core/auth/auth_types.h"
#include "src/core/auth/providers/wy/wy_profile.h"

#include <cstdint>
#include <string>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// WY HTTP 请求
struct Request {
    // 请求路径
    std::string path;
    // 编码前的表单请求体
    std::string body;
    // 本次请求 nonce
    std::string nonce;
    // 本次请求时间戳
    std::int64_t timestamp = 0;
    // 本次请求签名
    std::string sign;
};

// 构造登录请求
Request make_login_request(const Profile& profile, const LoginInput& input, std::int64_t now_seconds);
// 构造心跳请求
Request make_heartbeat_request(const Profile& profile, const SessionInput& input, std::int64_t now_seconds);
// 构造远程变量请求
Request make_var_request(const Profile& profile, const VarInput& input, std::int64_t now_seconds);
// 构造公告请求
Request make_notice_request(const Profile& profile);
// 构造更新检查请求
Request make_update_request(const Profile& profile);

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
