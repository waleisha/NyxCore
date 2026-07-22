#pragma once

#include "src/core/auth/auth_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nyx {
namespace core {
namespace auth {

// 登录会话状态
struct SessionState {
    // 会话是否处于激活状态
    bool active = false;
    // 用户授权码
    std::string license;
    // 当前设备 ID
    std::string device_id;
    // 服务端会话令牌
    std::string token;
    // 会话过期时间戳，0 表示不过期
    std::int64_t expires_at = 0;
    // 剩余使用次数
    int remaining_uses = 0;
    // 连续心跳失败次数
    int heartbeat_failures = 0;
    // 功能授权票据列表
    std::vector<FeatureTicket> features;

    // 判断会话是否仍然有效
    bool live() const;
    // 判断会话是否允许指定功能
    bool allows(const std::string& feature) const;
    // 清空会话
    void clear();
    // 用登录结果启动新会话
    void start(const std::string& next_license, const std::string& next_device_id, const ProviderResult& result);
    // 用心跳结果刷新当前会话
    void refresh(const ProviderResult& result);
    // 记录一次心跳失败，返回是否超过容忍次数
    bool record_heartbeat_failure(int limit);
    // 构造会话请求输入
    SessionInput session_input() const;
    // 构造远程变量请求输入
    VarInput var_input(const std::string& key) const;
};

// 当前 Unix 秒
std::int64_t now_seconds();
// 判断功能票据是否未过期
bool ticket_live(const FeatureTicket& ticket);

} // namespace auth
} // namespace core
} // namespace nyx
