#include "src/core/auth/auth_types.h"

#include "src/core/auth/auth_session.h"

#include <atomic>
#include <memory>

namespace nyx {
namespace core {
namespace auth {

namespace {

// mock 心跳失败次数，用于测试连续失败处理
std::atomic<int> g_heartbeat_failures{0};

// 构造功能授权票据
FeatureTicket ticket(const char* feature, bool allowed, std::int64_t expires_at) {
    FeatureTicket value;
    value.feature = feature;
    value.allowed = allowed;
    value.expires_at = expires_at;
    return value;
}

// 构造拒绝结果
ProviderResult rejected(int code, const char* message) {
    ProviderResult result;
    result.code = code;
    result.failure = sdk::auth::Err::Rejected;
    result.message = message;
    return result;
}

// 测试用 provider：不访问网络，返回固定授权和变量
class MockProvider final : public IProvider {
public:
    // 登录成功后返回一组覆盖允许、拒绝、过期的功能票据
    ProviderResult Login(const LoginInput& input) override {
        if (input.license.empty() || input.device_id.empty()) {
            return rejected(10, "invalid login input");
        }

        const auto now = now_seconds();
        ProviderResult result;
        result.success = true;
        result.message = "ok";
        result.token = "doctor-session";
        result.expires_at = now + 3600;
        result.remaining_uses = 32;
        result.server_time = now;
        result.features.push_back(ticket("camera_assist", true, now + 3600));
        result.features.push_back(ticket("blocked_feature", false, now + 3600));
        result.features.push_back(ticket("expired_feature", true, now - 1));
        return result;
    }

    // 心跳按全局计数模拟临时网络失败
    ProviderResult Heartbeat(const SessionInput& input) override {
        if (input.token.empty() || input.license.empty() || input.device_id.empty()) {
            return rejected(11, "invalid session input");
        }

        int pending = g_heartbeat_failures.load(std::memory_order_relaxed);
        while (pending > 0) {
            if (g_heartbeat_failures.compare_exchange_weak(
                    pending,
                    pending - 1,
                    std::memory_order_relaxed
                )) {
                ProviderResult result;
                result.code = 12;
                result.failure = sdk::auth::Err::Network;
                result.message = "temporary heartbeat failure";
                return result;
            }
        }

        const auto now = now_seconds();
        ProviderResult result;
        result.success = true;
        result.message = "ok";
        result.token = input.token;
        result.expires_at = now + 3600;
        result.remaining_uses = 32;
        result.server_time = now;
        return result;
    }

    // 返回测试远程变量
    ProviderResult GetVar(const VarInput& input) override {
        if (input.token.empty() || input.key.empty()) {
            return rejected(13, "invalid value input");
        }

        ProviderResult result;
        result.success = true;
        result.message = "ok";
        if (input.key == "payload_seed") {
            result.value = "doctor-seed";
        } else if (input.key == "feature_flag") {
            result.value = "enabled";
        } else {
            return rejected(14, "value not found");
        }

        return result;
    }
};

} // namespace

// 创建 mock provider
std::unique_ptr<IProvider> MakeMockProvider() {
    return std::make_unique<MockProvider>();
}

// 设置后续心跳需要失败的次数
void SetMockHeartbeatFailures(int count) {
    g_heartbeat_failures.store(count > 0 ? count : 0, std::memory_order_relaxed);
}

} // namespace auth
} // namespace core
} // namespace nyx
