#pragma once

#include "src/core/auth/auth_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// WY 响应解析结果
struct ParsedResponse {
    // 响应是否可解析
    bool valid = false;
    // 响应业务码
    int code = 0;
    // 服务端时间
    std::int64_t server_time = 0;
    // 是否包含服务端时间
    bool has_time = false;
    // 会话过期时间
    std::int64_t expires_at = 0;
    // 服务端 check 字段
    std::string check;
    // 是否包含 check 字段
    bool has_check = false;
    // 响应消息
    std::string message;
    // 会话令牌
    std::string token;
    // 业务载荷
    std::string value;
    // 剩余使用次数
    int remaining_uses = 0;
    // 功能授权票据
    std::vector<FeatureTicket> features;
};

// 解析 WY JSON 响应
bool parse(const std::string& body, ParsedResponse* out);

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
