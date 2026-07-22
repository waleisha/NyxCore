#pragma once

#include "src/core/auth/providers/wy/wy_profile.h"

#include <string>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// 按 profile 配置编码 WY 请求体
std::string encode(const Profile& profile, const std::string& plain);
// 按 profile 配置解码 WY 响应体
std::string decode(const Profile& profile, const std::string& encoded);
// 计算 MD5 十六进制摘要
std::string md5_hex(const std::string& text);
// 计算 SHA-256 十六进制摘要
std::string sha256_hex(const std::string& text);

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
