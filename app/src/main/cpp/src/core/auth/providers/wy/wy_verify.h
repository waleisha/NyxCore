#pragma once

#include "sdk/include/network.h"
#include "src/core/auth/providers/wy/wy_profile.h"
#include "src/core/auth/providers/wy/wy_request.h"
#include "src/core/auth/providers/wy/wy_response.h"

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// 校验登录响应 check
bool verify_login(const Profile& profile, const Request& request, const ParsedResponse& response);
// 校验心跳响应 check
bool verify_heartbeat(const Profile& profile, const Request& request, const ParsedResponse& response);
// 校验远程变量响应 check
bool verify_var(const Profile& profile, const Request& request, const ParsedResponse& response);
// 生成登录 ProviderResult
ProviderResult make_login_result(const Profile& profile, const Request& request, const ParsedResponse& response);
// 生成心跳 ProviderResult
ProviderResult make_heartbeat_result(const Profile& profile, const Request& request, const ParsedResponse& response);
// 生成远程变量 ProviderResult
ProviderResult make_var_result(const Profile& profile, const Request& request, const ParsedResponse& response);
// 生成公告 ProviderResult
ProviderResult make_notice_result(const Profile& profile, const Request& request, const ParsedResponse& response);
// 生成更新检查 ProviderResult
ProviderResult make_update_result(const Profile& profile, const Request& request, const ParsedResponse& response);
// 获取 WY 请求使用的 CA 证书
sdk::net::CaBundle trust_ca();

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
