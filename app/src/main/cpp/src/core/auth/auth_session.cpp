#include "src/core/auth/auth_session.h"

#include <chrono>
#include <utility>

namespace nyx {
namespace core {
namespace auth {

// 当前 Unix 秒
std::int64_t now_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

// 判断会话是否仍然有效
bool SessionState::live() const {
    if (!active) {
        return false;
    }

    return expires_at <= 0 || expires_at > now_seconds();
}

// 判断功能票据是否未过期
bool ticket_live(const FeatureTicket& ticket) {
    return ticket.expires_at <= 0 || ticket.expires_at > now_seconds();
}

// 判断会话是否允许指定功能
bool SessionState::allows(const std::string& feature) const {
    if (feature.empty() || !live()) {
        return false;
    }

    for (const auto& ticket : features) {
        if (ticket.feature == feature) {
            return ticket.allowed && ticket_live(ticket);
        }
    }

    return false;
}

// 清空会话
void SessionState::clear() {
    *this = SessionState{};
}

// 用登录结果启动新会话
void SessionState::start(
    const std::string& next_license,
    const std::string& next_device_id,
    const ProviderResult& result
) {
    SessionState next;
    next.active = true;
    next.license = next_license;
    next.device_id = next_device_id;
    next.token = result.token;
    next.expires_at = result.expires_at;
    next.remaining_uses = result.remaining_uses;
    next.features = result.features;
    *this = std::move(next);
}

// 用心跳结果刷新当前会话
void SessionState::refresh(const ProviderResult& result) {
    if (!result.token.empty()) {
        token = result.token;
    }

    if (result.expires_at > 0) {
        expires_at = result.expires_at;
    }

    remaining_uses = result.remaining_uses;
    heartbeat_failures = 0;
    if (!result.features.empty()) {
        features = result.features;
    }
}

// 记录一次心跳失败，返回是否超过容忍次数
bool SessionState::record_heartbeat_failure(int limit) {
    if (limit < 0) {
        limit = 0;
    }
    ++heartbeat_failures;
    // limit 表示允许连续失败的次数
    return heartbeat_failures > limit;
}

// 构造会话请求输入
SessionInput SessionState::session_input() const {
    SessionInput input;
    input.license = license;
    input.device_id = device_id;
    input.token = token;
    return input;
}

// 构造远程变量请求输入
VarInput SessionState::var_input(const std::string& key) const {
    VarInput input;
    input.license = license;
    input.device_id = device_id;
    input.token = token;
    input.key = key;
    return input;
}

} // namespace auth
} // namespace core
} // namespace nyx
