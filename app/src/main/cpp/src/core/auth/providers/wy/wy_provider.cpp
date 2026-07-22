#include "src/core/auth/providers/wy/wy_provider.h"

#include "sdk/include/network.h"
#include "src/core/auth/auth_session.h"
#include "src/core/auth/providers/wy/wy_codec.h"
#include "src/core/auth/providers/wy/wy_request.h"
#include "src/core/auth/providers/wy/wy_verify.h"

#include <memory>
#include <string>
#include <utility>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

// 规范化 host，强制使用 HTTPS
std::string base_url(const Profile& profile) {
    const std::string& host = profile.endpoint.host;
    if (host.rfind("https://", 0) == 0) {
        return host;
    }
    if (host.rfind("http://", 0) == 0) {
        return std::string("https://") + host.substr(7);
    }
    return std::string("https://") + host;
}

// 将网络层失败转为 provider 结果
ProviderResult network_error(const sdk::net::HttpResponse& response, const char* message) {
    ProviderResult result;
    const bool has_http_status = response.status > 0;
    result.code = has_http_status ? response.status : response.error_code;
    result.failure = sdk::auth::Err::Network;
    if (has_http_status) {
        result.message = std::string(message) + " (http " + std::to_string(response.status) + ")";
    } else if (response.error_code != 0) {
        result.message = std::string(message) + " (error " + std::to_string(response.error_code) + ")";
    } else {
        result.message = message;
    }
    return result;
}

// 构造协议错误
ProviderResult protocol_error(const char* message) {
    ProviderResult result;
    result.failure = sdk::auth::Err::Protocol;
    result.message = message;
    return result;
}

// 构造本地状态错误
ProviderResult local_state_error(const char* message) {
    ProviderResult result;
    result.failure = sdk::auth::Err::LocalState;
    result.message = message;
    return result;
}

// WY provider：负责组包、编码、发送、解析和校验响应
class WyProvider final : public IProvider {
public:
    // 保存运行时配置，调用时再打开成明文 profile
    explicit WyProvider(RuntimeProfile profile)
        : profile_(std::move(profile)) {}

    // 登录并建立会话
    ProviderResult Login(const LoginInput& input) override {
        return call_login(input);
    }

    // 刷新当前会话
    ProviderResult Heartbeat(const SessionInput& input) override {
        return call_heartbeat(input);
    }

    // 获取远程变量
    ProviderResult GetVar(const VarInput& input) override {
        return call_var(input);
    }

    // 获取公告
    ProviderResult GetNotice() override {
        return call_notice();
    }

    // 检查更新
    ProviderResult CheckUpdate() override {
        return call_update();
    }

private:
    // 登录调用
    ProviderResult call_login(const LoginInput& input) {
        const Profile profile = open_profile(profile_);
        const Request request = make_login_request(profile, input, now_seconds());
        return send_and_parse(
            profile,
            request,
            profile.calls.login.success_code,
            "wy login failed",
            [&profile, &request](const ParsedResponse& parsed) {
                return make_login_result(profile, request, parsed);
            }
        );
    }

    // 心跳调用
    ProviderResult call_heartbeat(const SessionInput& input) {
        const Profile profile = open_profile(profile_);
        const Request request = make_heartbeat_request(profile, input, now_seconds());
        return send_and_parse(
            profile,
            request,
            profile.calls.heartbeat.success_code,
            "wy heartbeat failed",
            [&profile, &request](const ParsedResponse& parsed) {
                return make_heartbeat_result(profile, request, parsed);
            }
        );
    }

    // 远程变量调用，先将本地别名映射成 WY 变量 key
    ProviderResult call_var(const VarInput& input) {
        const Profile profile = open_profile(profile_);
        VarInput next = input;
        const std::string mapped = resolve_var(profile, input.key);
        if (mapped.empty()) {
            return local_state_error("wy variable is not configured");
        }

        next.key = mapped;
        const Request request = make_var_request(profile, next, now_seconds());
        return send_and_parse(
            profile,
            request,
            profile.calls.var.success_code,
            "wy variable failed",
            [&profile, &request](const ParsedResponse& parsed) {
                return make_var_result(profile, request, parsed);
            }
        );
    }

    // 公告调用，未配置时返回本地状态错误
    ProviderResult call_notice() {
        const Profile profile = open_profile(profile_);
        if (!profile.calls.notice) {
            return local_state_error("wy notice is not configured");
        }

        const Request request = make_notice_request(profile);
        return send_and_parse(
            profile,
            request,
            profile.calls.notice->success_code,
            "wy notice failed",
            [&profile, &request](const ParsedResponse& parsed) {
                return make_notice_result(profile, request, parsed);
            }
        );
    }

    // 更新检查调用，未配置时返回本地状态错误
    ProviderResult call_update() {
        const Profile profile = open_profile(profile_);
        if (!profile.calls.update) {
            return local_state_error("wy update is not configured");
        }

        const Request request = make_update_request(profile);
        return send_and_parse(
            profile,
            request,
            profile.calls.update->success_code,
            "wy update failed",
            [&profile, &request](const ParsedResponse& parsed) {
                return make_update_result(profile, request, parsed);
            }
        );
    }

    // 发送 WY 请求并完成解码、解析、结果转换
    template <typename Fn>
    ProviderResult send_and_parse(
        const Profile& profile,
        const Request& request,
        int expected_code,
        const char* fallback_message,
        Fn&& make_result
    ) {
        if (!is_configured(profile)) {
            return protocol_error("wy profile is not configured");
        }

        const std::string encoded = encode(profile, request.body);
        if (encoded.empty() && !request.body.empty()) {
            return protocol_error("wy request encode failed");
        }

        const sdk::net::HttpResponse response = sdk::net::Post(
            base_url(profile),
            request.path,
            encoded,
            3,
            5,
            64 * 1024,
            trust_ca()
        );
        if (response.error_code != 0 || response.status < 200 || response.status >= 300) {
            return network_error(response, fallback_message);
        }

        const std::string decoded = decode(profile, response.body);
        if (decoded.empty() && !response.body.empty()) {
            return protocol_error("wy response decode failed");
        }

        ParsedResponse parsed;
        if (!parse(decoded, &parsed)) {
            return protocol_error("wy response parse failed");
        }

        ProviderResult result = make_result(parsed);
        result.raw = decoded;
        if (expected_code > 0 && result.code == 0) {
            result.code = expected_code;
        }
        if (result.message.empty()) {
            result.message = fallback_message;
        }
        return result;
    }

    // 运行时配置
    RuntimeProfile profile_;
};

} // namespace

// 使用明文 profile 创建 WY provider
std::unique_ptr<IProvider> make_provider(Profile profile) {
    profile = with_defaults(std::move(profile));
    if (!is_configured(profile)) {
        return nullptr;
    }

    return std::make_unique<WyProvider>(store_profile(std::move(profile)));
}

// 使用运行时 profile 创建 WY provider
std::unique_ptr<IProvider> make_provider(RuntimeProfile profile) {
    if (!is_configured(open_profile(profile))) {
        return nullptr;
    }

    return std::make_unique<WyProvider>(std::move(profile));
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx

namespace nyx {
namespace core {
namespace auth {

// 使用默认配置创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider() {
    return wy::make_provider(wy::load_profile());
}

// 使用明文 profile 创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider(wy::Profile profile) {
    return wy::make_provider(std::move(profile));
}

// 使用运行时 profile 创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider(wy::RuntimeProfile profile) {
    return wy::make_provider(std::move(profile));
}

} // namespace auth
} // namespace core
} // namespace nyx
