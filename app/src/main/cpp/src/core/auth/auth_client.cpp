#include "src/core/auth/auth_client.h"

#include "sdk/include/utils.h"
#include "src/core/auth/auth_context.h"
#include "src/core/auth/auth_types.h"
#include "src/core/auth/auth_session.h"
#include "src/core/auth/auth_store.h"
#include "src/core/auth/auth_device.h"
#include "src/core/auth/providers/wy/wy_profile.h"
#include "src/core/context.h"

#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#ifndef NYX_AUTH_ENABLE_MOCK
#define NYX_AUTH_ENABLE_MOCK 0
#endif

namespace nyx {
namespace core {
namespace auth {

#if NYX_AUTH_ENABLE_MOCK
// 创建 mock provider
std::unique_ptr<IProvider> MakeMockProvider();
// 设置 mock provider 心跳失败次数
void SetMockHeartbeatFailures(int count);
#endif
// 创建默认 WY provider
std::unique_ptr<IProvider> MakeWyProvider();
// 使用运行时配置创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider(wy::RuntimeProfile profile);
// 判断当前会话是否授权指定功能
bool is_feature_licensed(const std::string& feature);

namespace {

// Provider 运行模式
enum class ProviderMode {
    // 不使用 provider
    None,
#if NYX_AUTH_ENABLE_MOCK
    // 使用 mock provider
    Mock,
#endif
    // 使用 WY provider
    Wy,
};

// 鉴权客户端内部状态
struct ClientState {
    // 鉴权运行上下文
    ContextState context;
    // 当前登录会话
    SessionState session;
    // 当前设备 ID
    std::string device_id;
    // 当前 provider 模式
    ProviderMode mode = ProviderMode::None;
    // 当前 provider 实例
    std::shared_ptr<IProvider> provider;
    // 待应用的 WY 运行时配置
    std::optional<wy::RuntimeProfile> pending_wy_profile;
    // 状态代数，用于丢弃过期远程响应
    std::uint64_t generation = 0;
};

// 轻量混合函数，用于把会话字段压成能力票据材料
std::uint64_t mix_u64(std::uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return value;
}

std::uint64_t mix_string(std::uint64_t seed, const std::string& value) {
    std::uint64_t h = seed ^ 0x9e3779b97f4a7c15ULL;
    for (unsigned char c : value) {
        h ^= static_cast<std::uint64_t>(c) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h = mix_u64(h);
    }
    return h ^ static_cast<std::uint64_t>(value.size());
}

bool same_u64(std::uint64_t left, std::uint64_t right) {
    std::uint64_t diff = left ^ right;
    diff |= diff >> 32;
    diff |= diff >> 16;
    diff |= diff >> 8;
    diff |= diff >> 4;
    diff |= diff >> 2;
    diff |= diff >> 1;
    return (diff & 1ULL) == 0;
}

// 可保存的客户端快照状态
struct SnapshotState {
    // 鉴权运行上下文
    ContextState context;
    // 当前登录会话
    SessionState session;
    // 当前设备 ID
    std::string device_id;
    // 当前 provider 模式
    ProviderMode mode = ProviderMode::None;
    // 待应用的 WY 运行时配置
    std::optional<wy::RuntimeProfile> pending_wy_profile;
};

// Provider 调用快照：远程调用在锁外执行，回写前用它校验状态
struct ProviderCall {
    // 调用创建时的上下文
    ContextState context;
    // 调用创建时的 provider
    std::shared_ptr<IProvider> provider;
    // 调用创建时的状态代数，防止迟到响应恢复已登出或重配的会话
    std::uint64_t generation = 0;
};

// 登录调用快照
struct LoginCall : ProviderCall {
    // 登录请求输入
    LoginInput input;
};

// 会话调用快照
struct SessionCall : ProviderCall {
    // 会话请求输入
    SessionInput input;
};

// 远程变量调用快照
struct VarCall : ProviderCall {
    // 远程变量请求输入
    VarInput input;
};

// WY provider 配置转换结果
struct WyConfig {
    // 运行时 profile，空值表示使用默认 provider 配置
    std::optional<wy::RuntimeProfile> profile;
};

// provider 未配置错误码
constexpr int kNoProviderCode = -2001;
// 上下文无效错误码
constexpr int kInvalidContextCode = -2002;
// 输入无效错误码
constexpr int kInvalidInputCode = -2003;
// 运行期状态变化错误码
constexpr int kRuntimeCode = -2004;
// 会话不存在错误码
constexpr int kNoSessionCode = -2005;
// 输出缓冲区错误码
constexpr int kOutputCode = -2006;
// 设备 ID 错误码
constexpr int kDeviceCode = -2007;
// 心跳连续失败容忍次数
constexpr int kHeartbeatFailureLimit = 2;
// 能力票据有效期，避免登录后长期复用旧票据
constexpr std::int64_t kCapabilityFreshSec = 15;

std::uint64_t session_seal(const ClientState& state) {
    const auto& session = state.session;
    std::uint64_t h = 0x6a09e667f3bcc909ULL;
    h = mix_string(h, session.license);
    h = mix_string(h, session.device_id);
    h = mix_string(h, session.token);
    h = mix_string(h, state.context.fingerprint());
    h ^= mix_u64(static_cast<std::uint64_t>(session.expires_at));
    h ^= mix_u64(static_cast<std::uint64_t>(session.remaining_uses) << 17u);
    h ^= mix_u64(static_cast<std::uint64_t>(session.heartbeat_failures) << 23u);
    h ^= mix_u64(state.generation ^ 0xbb67ae8584caa73bULL);
    return mix_u64(h);
}

std::uint64_t capability_word(
    const ClientState& state,
    sdk::auth::CapabilityPurpose purpose,
    std::uint64_t salt,
    std::int64_t issued_at,
    std::uint32_t flags
) {
    const auto& session = state.session;
    std::uint64_t h = 0xbb67ae8584caa73bULL ^ salt;
    h = mix_string(h, session.license);
    h = mix_string(h, session.device_id);
    h = mix_string(h, session.token);
    h = mix_string(h, state.context.fingerprint());
    h ^= mix_u64(static_cast<std::uint64_t>(purpose));
    h ^= mix_u64(static_cast<std::uint64_t>(issued_at));
    h ^= mix_u64(static_cast<std::uint64_t>(flags) << 32u);
    h ^= mix_u64(static_cast<std::uint64_t>(session.expires_at));
    h ^= mix_u64(static_cast<std::uint64_t>(session.remaining_uses) << 13u);
    h ^= mix_u64(session_seal(state) ^ 0xa54ff53a5f1d36f1ULL);
    return mix_u64(h);
}

std::uint64_t capability_binding(
    const ClientState& state,
    sdk::auth::CapabilityPurpose purpose,
    std::uint64_t salt
) {
    const auto& session = state.session;
    std::uint64_t h = 0x1f83d9abfb41bd6bULL ^ salt;
    h = mix_string(h, session.device_id);
    h = mix_string(h, session.token);
    h = mix_string(h, state.context.fingerprint());
    h ^= mix_u64(static_cast<std::uint64_t>(purpose));
    h ^= mix_u64(static_cast<std::uint64_t>(session.expires_at));
    h ^= mix_u64(static_cast<std::uint64_t>(session.remaining_uses) << 19u);
    return mix_u64(h);
}

std::uint64_t capability_checksum(const ClientState& state, const sdk::auth::Capability& cap) {
    std::uint64_t h = 0x510e527fade682d1ULL;
    h ^= mix_u64(cap.word0 ^ 0x9b05688c2b3e6c1fULL);
    h ^= mix_u64(cap.word1 ^ 0x1f83d9abfb41bd6bULL);
    h ^= mix_u64(cap.word2 ^ 0x5be0cd19137e2179ULL);
    h ^= mix_u64(cap.binding0 ^ 0xcbbb9d5dc1059ed8ULL);
    h ^= mix_u64(cap.binding1 ^ 0x629a292a367cd507ULL);
    h ^= mix_u64(static_cast<std::uint64_t>(cap.issued_at));
    h ^= mix_u64((static_cast<std::uint64_t>(cap.purpose) << 32u) | cap.flags);
    h ^= mix_u64(session_seal(state) ^ state.generation);
    return mix_u64(h);
}

std::uint32_t capability_flags(const ClientState& state) {
    return static_cast<std::uint32_t>((state.session.heartbeat_failures & 0xFF) << 8);
}

sdk::auth::Capability make_capability(
    const ClientState& state,
    sdk::auth::CapabilityPurpose purpose,
    std::int64_t issued_at
) {
    sdk::auth::Capability cap;
    cap.issued_at = issued_at;
    cap.purpose = static_cast<std::uint32_t>(purpose);
    cap.flags = capability_flags(state);
    cap.word0 = capability_word(state, purpose, 0x243f6a8885a308d3ULL, cap.issued_at, cap.flags);
    cap.word1 = capability_word(state, purpose, cap.word0 ^ 0x13198a2e03707344ULL, cap.issued_at, cap.flags);
    cap.word2 = capability_word(state, purpose, cap.word1 ^ 0xa4093822299f31d0ULL, cap.issued_at, cap.flags);
    cap.binding0 = capability_binding(state, purpose, 0x452821e638d01377ULL);
    cap.binding1 = capability_binding(state, purpose, cap.binding0 ^ 0xbe5466cf34e90c6cULL);
    cap.checksum = capability_checksum(state, cap);
    return cap;
}

bool has_capability_material(const sdk::auth::Capability& cap) {
    return cap.issued_at > 0 &&
        cap.word0 != 0 &&
        cap.word1 != 0 &&
        cap.word2 != 0 &&
        cap.binding0 != 0 &&
        cap.binding1 != 0 &&
        cap.checksum != 0;
}

// 主线程和渲染线程不允许执行可能阻塞的鉴权调用
bool is_blocked_thread() {
    return sdk::utils::IsMain() || core::Context::instance().is_render_thread();
}

// provider 模式对应的本地存储名称
std::string provider_name(ProviderMode mode) {
    switch (mode) {
#if NYX_AUTH_ENABLE_MOCK
        case ProviderMode::Mock:
            return "mock";
#endif
        case ProviderMode::Wy:
            return "wy";
        case ProviderMode::None:
            break;
    }

    return {};
}

// SDK 整数配置转为 WY 签名校验类型
wy::CheckKind check_kind(int value) {
    switch (value) {
        case 0:
            return wy::CheckKind::TimeAppKeyNonceSalt;
        case 1:
            return wy::CheckKind::AppKeyNonceSalt;
        case 2:
            return wy::CheckKind::TimeAppKeyNonce;
        default:
            return static_cast<wy::CheckKind>(value);
    }
}

// 判断 SDK WY 配置是否携带了任何显式字段
bool has_wy_profile(const sdk::auth::WyProfile& profile) {
    return !profile.host.empty() ||
        !profile.path_prefix.empty() ||
        !profile.api_token.empty() ||
        !profile.app_key.empty() ||
        !profile.login_call_id.empty() ||
        profile.login_success_code != 0 ||
        !profile.heartbeat_call_id.empty() ||
        profile.heartbeat_success_code != 0 ||
        !profile.var_call_id.empty() ||
        profile.var_success_code != 0 ||
        !profile.notice_call_id.empty() ||
        profile.notice_success_code != 0 ||
        !profile.update_call_id.empty() ||
        profile.update_success_code != 0 ||
        profile.login_check_kind != 0 ||
        profile.heartbeat_check_kind != 2 ||
        profile.var_check_kind != 2 ||
        !profile.salt.empty() ||
        !profile.rc4_key.empty() ||
        !profile.alphabet.empty() ||
        !profile.license_field.empty() ||
        !profile.device_field.empty() ||
        !profile.token_field.empty() ||
        !profile.time_field.empty() ||
        !profile.sign_field.empty() ||
        !profile.nonce_field.empty() ||
        !profile.var_field.empty() ||
        !profile.vars.empty();
}

// 将 SDK WY 配置转换成 provider 内部配置
wy::Profile wy_profile_from_sdk(const sdk::auth::WyProfile& in) {
    wy::Profile out;
    out.endpoint.host = in.host;
    out.endpoint.path_prefix = in.path_prefix;
    out.endpoint.api_token = in.api_token;
    out.app_key = in.app_key;

    out.calls.login = wy::Call{in.login_call_id, in.login_success_code};
    out.calls.heartbeat = wy::Call{in.heartbeat_call_id, in.heartbeat_success_code};
    out.calls.var = wy::Call{in.var_call_id, in.var_success_code};
    if (!in.notice_call_id.empty() && in.notice_success_code > 0) {
        out.calls.notice = wy::Call{in.notice_call_id, in.notice_success_code};
    }
    if (!in.update_call_id.empty() && in.update_success_code > 0) {
        out.calls.update = wy::Call{in.update_call_id, in.update_success_code};
    }

    out.check.login = check_kind(in.login_check_kind);
    out.check.heartbeat = check_kind(in.heartbeat_check_kind);
    out.check.var = check_kind(in.var_check_kind);
    out.check.salt = in.salt;

    out.codec.rc4_key = in.rc4_key;
    out.codec.alphabet = in.alphabet;

    out.fields.license = in.license_field;
    out.fields.device = in.device_field;
    out.fields.token = in.token_field;
    out.fields.time = in.time_field;
    out.fields.sign = in.sign_field;
    out.fields.nonce = in.nonce_field;
    out.fields.var_key = in.var_field;

    for (const auto& entry : in.vars) {
        if (!entry.first.empty() && !entry.second.empty()) {
            out.vars[entry.first] = entry.second;
        }
    }

    return wy::with_defaults(std::move(out));
}

// 解析并校验 WY 配置
bool make_wy_config(
    const sdk::auth::InitConfig& config,
    WyConfig* out,
    sdk::auth::Result* error
) {
    out->profile.reset();
    if (!has_wy_profile(config.profile)) {
        return true;
    }

    wy::Profile next = wy_profile_from_sdk(config.profile);
    if (!wy::is_configured(next)) {
        *error = fail(kInvalidInputCode, sdk::auth::Err::Rejected, "WY auth profile is invalid");
        return false;
    }

    out->profile = wy::store_profile(std::move(next));
    return true;
}

// 构造上下文缺失详情
std::string context_detail(const ContextState& context) {
    std::string detail = "auth context is missing";
    if (context.files_dir.empty()) {
        detail.append(" files_dir");
    }
    if (context.package_name.empty()) {
        detail.append(" package_name");
    }
    return detail;
}

// 根据模式创建 provider 实例
std::shared_ptr<IProvider> make_provider(
    ProviderMode mode,
    const std::optional<wy::RuntimeProfile>& wy_profile
) {
    switch (mode) {
#if NYX_AUTH_ENABLE_MOCK
        case ProviderMode::Mock:
            return std::shared_ptr<IProvider>(MakeMockProvider());
#endif
        case ProviderMode::Wy:
            if (wy_profile) {
                return std::shared_ptr<IProvider>(MakeWyProvider(*wy_profile));
            }
            return std::shared_ptr<IProvider>(MakeWyProvider());
        case ProviderMode::None:
            break;
    }

    return nullptr;
}

// 只清理内存中的会话
void clear_state_session(ClientState& state) {
    state.session.clear();
}

// 清理内存会话和本地会话封存文件
void clear_stored_session(ClientState& state) {
    state.session.clear();
    if (state.context.is_valid()) {
        clear_session_store(state.context);
    }
}

// 选择 provider，创建失败时回退到无 provider 状态
void select_provider(ClientState& state, ProviderMode mode) {
    state.mode = mode;
    state.provider = make_provider(mode, state.pending_wy_profile);
    if (state.provider == nullptr && mode == ProviderMode::Wy) {
        state.mode = ProviderMode::None;
        clear_state_session(state);
    }
}

// Provider 结果转 SDK 结果
sdk::auth::Result from_provider(const ProviderResult& result) {
    return make_result(result.success, result.code, result.failure, result.message);
}

// 将文本复制到调用方缓冲区
sdk::auth::Result copy_text(
    const ProviderResult& result,
    const std::string& text,
    char* out,
    std::size_t out_len
) {
    if (out == nullptr || out_len == 0 || text.size() + 1 > out_len) {
        return fail(kOutputCode, sdk::auth::Err::LocalState, "output buffer is too small");
    }

    std::memcpy(out, text.c_str(), text.size() + 1);
    return from_provider(result);
}

// 成功结果才复制载荷，失败时保留 provider 错误
sdk::auth::Result copy_payload(
    const ProviderResult& result,
    const std::string& payload,
    char* out,
    std::size_t out_len
) {
    if (!result.success) {
        return from_provider(result);
    }

    return copy_text(result, payload, out, out_len);
}

// 确保当前状态有设备 ID
DeviceIdResult ensure_device(ClientState& state) {
    if (!state.device_id.empty()) {
        DeviceIdResult result;
        result.success = true;
        result.id = state.device_id;
        return result;
    }

    DeviceIdResult result = ensure_device_id(state.context);
    if (result.success) {
        state.device_id = result.id;
    }
    return result;
}

// 尝试从本地封存文件恢复会话
void restore_session(ClientState& state) {
    if (!state.context.is_valid()) {
        clear_state_session(state);
        return;
    }

    const std::string name = provider_name(state.mode);
    if (name.empty()) {
        return;
    }

    const auto device = ensure_device(state);
    if (!device.success) {
        clear_state_session(state);
        return;
    }

    SessionState restored;
    if (load_session(state.context, name, state.device_id, &restored)) {
        state.session = std::move(restored);
    } else if (!state.session.live()) {
        clear_state_session(state);
    }
}

// 保存当前会话到本地封存文件
void save_current_session(const ClientState& state) {
    const std::string name = provider_name(state.mode);
    if (!name.empty()) {
        save_session(state.context, name, state.session);
    }
}

// 判断两个会话请求是否指向同一会话
bool same_session_input(const SessionInput& left, const SessionInput& right) {
    return left.license == right.license &&
        left.device_id == right.device_id &&
        left.token == right.token;
}

// 重置 mock provider 的可变状态
void reset_mock_state() {
#if NYX_AUTH_ENABLE_MOCK
    SetMockHeartbeatFailures(0);
#endif
}

class AuthClient final {
public:
    // 配置鉴权上下文和 provider
    sdk::auth::Result configure(const sdk::auth::Context& context, const sdk::auth::InitConfig& config);
    // 登录并建立会话
    sdk::auth::Result login(const char* license);
    // 登出并清理本地会话
    void logout();
    // 判断当前是否有有效会话
    bool has_session() const;
    // 判断当前会话是否授权指定功能
    bool is_feature_licensed(const std::string& feature) const;
    // 导出短生命周期能力票据
    sdk::auth::Value<sdk::auth::Capability> export_capability(sdk::auth::CapabilityPurpose purpose) const;
    // 校验短生命周期能力票据
    bool verify_capability(sdk::auth::CapabilityPurpose purpose, const sdk::auth::Capability& capability) const;
    // 判断鉴权客户端是否已准备好
    bool is_ready() const;
    // 获取远程变量
    sdk::auth::Result fetch_var(const char* key, char* out, std::size_t out_len);
    // 获取公告
    sdk::auth::Result fetch_notice(char* out, std::size_t out_len);
    // 检查更新
    sdk::auth::Result fetch_update(char* out, std::size_t out_len);
    // 执行心跳
    sdk::auth::Result heartbeat();

    // 保存当前客户端状态快照
    std::shared_ptr<SnapshotState> save_snapshot() const;
    // 恢复客户端状态快照
    void restore_snapshot(const SnapshotState* snapshot);
    // 重置客户端状态
    void reset();
    // 切换到无 provider 模式
    void use_no_provider();
    // 切换到 mock provider
    void use_mock();
    // 设置 mock 心跳失败次数
    void set_heartbeat_failures(int count);
    // 设置当前线程的渲染线程标记
    void set_render_thread(bool enabled);
    // 获取当前设备 ID
    std::string device_id();
    // 判断本地会话封存文件是否存在
    bool has_session_store() const;
    // 获取本地会话封存文件路径
    std::string session_store_path() const;

private:
    // 准备不依赖会话的 provider 调用
    bool prepare_provider_call(ProviderCall* call, sdk::auth::Result* error) const;
    // 准备登录调用
    bool prepare_login_call(const std::string& license, LoginCall* call, sdk::auth::Result* error);
    // 准备心跳等会话调用
    bool prepare_session_call(SessionCall* call, sdk::auth::Result* error) const;
    // 准备远程变量调用
    bool prepare_var_call(const std::string& key, VarCall* call, sdk::auth::Result* error) const;
    // 确保上下文有效，调用方必须持有 mutex_
    bool ensure_context_locked(sdk::auth::Result* error) const;
    // 确保 provider 可用，调用方必须持有 mutex_
    bool ensure_provider_locked(sdk::auth::Result* error) const;
    // 确保会话有效，调用方必须持有 mutex_
    bool ensure_session_locked(sdk::auth::Result* error) const;
    // 填充 provider 调用快照，调用方必须持有 mutex_
    bool fill_provider_call_locked(ProviderCall* call, sdk::auth::Result* error) const;
    // 填充会话调用快照，调用方必须持有 mutex_
    bool fill_session_call_locked(ProviderCall* call, sdk::auth::Result* error) const;
    // 判断远程变量请求是否仍匹配当前会话，调用方必须持有 mutex_
    bool session_matches_locked(const ContextState& context, const VarInput& input) const;

    // 执行无会话载荷获取，回写前确认 provider 状态未变化
    template <typename Fetch>
    sdk::auth::Result fetch_payload(char* out, std::size_t out_len, Fetch&& fetch) {
        if (is_blocked_thread()) {
            return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth call rejected on this thread");
        }

        ProviderCall call;
        sdk::auth::Result error;
        if (!prepare_provider_call(&call, &error)) {
            return error;
        }

        const auto result = fetch(*call.provider);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_.generation != call.generation ||
                state_.context != call.context ||
                state_.provider != call.provider) {
                return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth state changed during fetch");
            }
        }
        const std::string payload = !result.value.empty() ? result.value : result.message;
        return copy_payload(result, payload, out, out_len);
    }

    // 保护客户端状态的锁
    mutable std::mutex mutex_;
    // 客户端内部状态
    ClientState state_;
};

// 配置鉴权上下文和 provider
sdk::auth::Result AuthClient::configure(
    const sdk::auth::Context& context,
    const sdk::auth::InitConfig& config
) {
    WyConfig wy_config;
    sdk::auth::Result config_error;
    if (!make_wy_config(config, &wy_config, &config_error)) {
        return config_error;
    }

    ContextState next = ContextState::from(context);

    ClientState restore_state;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.context != next) {
            clear_state_session(state_);
            state_.device_id.clear();
        }

        state_.context = std::move(next);
        state_.pending_wy_profile = std::move(wy_config.profile);
#if NYX_AUTH_ENABLE_MOCK
        if (state_.mode == ProviderMode::Mock && !state_.pending_wy_profile) {
            select_provider(state_, ProviderMode::Mock);
        } else
#endif
        {
            select_provider(state_, ProviderMode::Wy);
        }
        generation = ++state_.generation;
        restore_state = state_;
    }

    restore_session(restore_state);

    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.generation != generation ||
        state_.context != restore_state.context ||
        state_.mode != restore_state.mode) {
        return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth context changed during init");
    }

    state_.device_id = std::move(restore_state.device_id);
    state_.session = std::move(restore_state.session);
    ++state_.generation;

    if (!state_.context.is_valid()) {
        return fail(kInvalidContextCode, sdk::auth::Err::LocalState, context_detail(state_.context));
    }
    if (state_.provider == nullptr) {
        return fail(kNoProviderCode, sdk::auth::Err::LocalState, "auth provider is not configured");
    }
    return ok("configured");
}

// 登录并建立会话
sdk::auth::Result AuthClient::login(const char* license_text) {
    if (is_blocked_thread()) {
        return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth call rejected on this thread");
    }

    const std::string license = copy_value(license_text);
    if (license.empty()) {
        return fail(kInvalidInputCode, sdk::auth::Err::Rejected, "license is empty");
    }

    LoginCall call;
    sdk::auth::Result error;
    if (!prepare_login_call(license, &call, &error)) {
        return error;
    }

    const auto result = call.provider->Login(call.input);

    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.generation != call.generation ||
        state_.context != call.context ||
        state_.provider != call.provider) {
        return fail(
            kRuntimeCode,
            sdk::auth::Err::Runtime,
            "auth state changed during login"
        );
    }

    if (result.success) {
        state_.session.start(license, call.input.device_id, result);
        save_current_session(state_);
    } else {
        clear_stored_session(state_);
    }
    ++state_.generation;

    return from_provider(result);
}

// 登出并清理本地会话
void AuthClient::logout() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_stored_session(state_);
    ++state_.generation;
}

// 判断当前是否有有效会话
bool AuthClient::has_session() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.session.live();
}

// 判断当前会话是否授权指定功能
bool AuthClient::is_feature_licensed(const std::string& feature) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.session.allows(feature);
}

// 导出短生命周期能力票据
sdk::auth::Value<sdk::auth::Capability> AuthClient::export_capability(
    sdk::auth::CapabilityPurpose purpose
) const {
    sdk::auth::Value<sdk::auth::Capability> out;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.context.is_valid() || !state_.session.live()) {
        out.result = fail(kNoSessionCode, sdk::auth::Err::LocalState, "auth session is not active");
        return out;
    }

    out.value = make_capability(state_, purpose, now_seconds());
    out.result = ok("capability exported");
    return out;
}

// 校验短生命周期能力票据
bool AuthClient::verify_capability(
    sdk::auth::CapabilityPurpose purpose,
    const sdk::auth::Capability& capability
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.context.is_valid() || !state_.session.live() || !has_capability_material(capability)) {
        return false;
    }
    if (capability.purpose != static_cast<std::uint32_t>(purpose)) {
        return false;
    }

    const std::int64_t now = now_seconds();
    if (capability.issued_at > now + 2 || now - capability.issued_at > kCapabilityFreshSec) {
        return false;
    }

    const sdk::auth::Capability expected = make_capability(state_, purpose, capability.issued_at);
    return capability.flags == expected.flags &&
        same_u64(capability.word0, expected.word0) &&
        same_u64(capability.word1, expected.word1) &&
        same_u64(capability.word2, expected.word2) &&
        same_u64(capability.binding0, expected.binding0) &&
        same_u64(capability.binding1, expected.binding1) &&
        same_u64(capability.checksum, expected.checksum);
}

// 判断鉴权客户端是否已准备好
bool AuthClient::is_ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.context.is_valid() && state_.provider != nullptr;
}

// 获取远程变量
sdk::auth::Result AuthClient::fetch_var(const char* key, char* out, std::size_t out_len) {
    if (is_blocked_thread()) {
        return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth call rejected on this thread");
    }

    const std::string name = copy_value(key);
    if (name.empty()) {
        return fail(kInvalidInputCode, sdk::auth::Err::Rejected, "value key is empty");
    }

    VarCall call;
    sdk::auth::Result error;
    if (!prepare_var_call(name, &call, &error)) {
        return error;
    }

    const auto result = call.provider->GetVar(call.input);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.generation != call.generation ||
            state_.context != call.context ||
            state_.provider != call.provider) {
            return fail(
                kRuntimeCode,
                sdk::auth::Err::Runtime,
                "auth state changed during fetch"
            );
        }
        if (!session_matches_locked(call.context, call.input)) {
            return fail(
                kNoSessionCode,
                sdk::auth::Err::LocalState,
                "auth session changed during fetch"
            );
        }
    }

    const std::string payload = !result.value.empty() ? result.value : result.message;
    return copy_payload(result, payload, out, out_len);
}

// 获取公告
sdk::auth::Result AuthClient::fetch_notice(char* out, std::size_t out_len) {
    return fetch_payload(out, out_len, [](IProvider& provider) {
        return provider.GetNotice();
    });
}

// 检查更新
sdk::auth::Result AuthClient::fetch_update(char* out, std::size_t out_len) {
    return fetch_payload(out, out_len, [](IProvider& provider) {
        return provider.CheckUpdate();
    });
}

// 执行心跳，并按连续失败策略保存或清理会话
sdk::auth::Result AuthClient::heartbeat() {
    if (is_blocked_thread()) {
        return fail(kRuntimeCode, sdk::auth::Err::Runtime, "auth call rejected on this thread");
    }

    SessionCall call;
    sdk::auth::Result error;
    if (!prepare_session_call(&call, &error)) {
        return error;
    }

    const auto result = call.provider->Heartbeat(call.input);

    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.generation != call.generation ||
        state_.context != call.context ||
        state_.provider != call.provider ||
        !same_session_input(state_.session.session_input(), call.input)) {
        return fail(
            kRuntimeCode,
            sdk::auth::Err::Runtime,
            "auth state changed during heartbeat"
        );
    }

    if (result.success) {
        state_.session.refresh(result);
        save_current_session(state_);
        ++state_.generation;
        return from_provider(result);
    }

    if (result.failure == sdk::auth::Err::Rejected) {
        clear_stored_session(state_);
        ++state_.generation;
        return from_provider(result);
    }

    if (state_.session.record_heartbeat_failure(kHeartbeatFailureLimit)) {
        clear_stored_session(state_);
    } else {
        save_current_session(state_);
    }
    ++state_.generation;

    return from_provider(result);
}

// 保存当前客户端状态快照
std::shared_ptr<SnapshotState> AuthClient::save_snapshot() const {
    auto copy = std::make_shared<SnapshotState>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy->context = state_.context;
        copy->session = state_.session;
        copy->device_id = state_.device_id;
        copy->mode = state_.mode;
        copy->pending_wy_profile = state_.pending_wy_profile;
    }
    return copy;
}

// 恢复客户端状态快照
void AuthClient::restore_snapshot(const SnapshotState* snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot == nullptr) {
        const std::uint64_t next_generation = state_.generation + 1;
        state_ = ClientState{};
        state_.generation = next_generation;
        reset_mock_state();
        return;
    }

    state_.context = snapshot->context;
    state_.session = snapshot->session;
    state_.device_id = snapshot->device_id;
    state_.pending_wy_profile = snapshot->pending_wy_profile;
    select_provider(state_, snapshot->mode);
    ++state_.generation;
    reset_mock_state();
}

// 重置客户端状态
void AuthClient::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t next_generation = state_.generation + 1;
    state_ = ClientState{};
    // generation 不能归零，已经发出的远程调用必须永久失效
    state_.generation = next_generation;
    reset_mock_state();
}

// 切换到无 provider 模式
void AuthClient::use_no_provider() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.pending_wy_profile.reset();
    state_.mode = ProviderMode::None;
    state_.provider = nullptr;
    clear_state_session(state_);
    ++state_.generation;
}

// 切换到 mock provider
void AuthClient::use_mock() {
#if NYX_AUTH_ENABLE_MOCK
    std::lock_guard<std::mutex> lock(mutex_);
    select_provider(state_, ProviderMode::Mock);
    ++state_.generation;
    restore_session(state_);
#endif
}

// 设置 mock 心跳失败次数
void AuthClient::set_heartbeat_failures(int count) {
#if NYX_AUTH_ENABLE_MOCK
    SetMockHeartbeatFailures(count);
#else
    (void)count;
#endif
}

// 设置当前线程的渲染线程标记
void AuthClient::set_render_thread(bool enabled) {
    core::Context::instance().set_render_thread(enabled);
}

// 获取当前设备 ID
std::string AuthClient::device_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto device = ensure_device(state_);
    return device.success ? device.id : std::string();
}

// 判断本地会话封存文件是否存在
bool AuthClient::has_session_store() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ::nyx::core::auth::has_session_store(state_.context);
}

// 获取本地会话封存文件路径
std::string AuthClient::session_store_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ::nyx::core::auth::session_store_path(state_.context);
}

// 准备不依赖会话的 provider 调用
bool AuthClient::prepare_provider_call(ProviderCall* call, sdk::auth::Result* error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fill_provider_call_locked(call, error);
}

// 准备登录调用
bool AuthClient::prepare_login_call(
    const std::string& license,
    LoginCall* call,
    sdk::auth::Result* error
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fill_provider_call_locked(call, error)) {
        return false;
    }

    const auto device = ensure_device(state_);
    if (!device.success) {
        *error = fail(
            kDeviceCode,
            sdk::auth::Err::LocalState,
            device.message.empty() ? "device id is not available" : device.message
        );
        return false;
    }

    call->input.license = license;
    call->input.device_id = state_.device_id;
    return true;
}

// 准备心跳等会话调用
bool AuthClient::prepare_session_call(SessionCall* call, sdk::auth::Result* error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fill_session_call_locked(call, error)) {
        return false;
    }

    call->input = state_.session.session_input();
    return true;
}

// 准备远程变量调用
bool AuthClient::prepare_var_call(
    const std::string& key,
    VarCall* call,
    sdk::auth::Result* error
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fill_session_call_locked(call, error)) {
        return false;
    }

    call->input = state_.session.var_input(key);
    return true;
}

// 确保上下文有效，调用方必须持有 mutex_
bool AuthClient::ensure_context_locked(sdk::auth::Result* error) const {
    if (!state_.context.is_valid()) {
        *error = fail(
            kInvalidContextCode,
            sdk::auth::Err::LocalState,
            "auth context is not configured"
        );
        return false;
    }

    return true;
}

// 确保 provider 可用，调用方必须持有 mutex_
bool AuthClient::ensure_provider_locked(sdk::auth::Result* error) const {
    if (state_.provider == nullptr) {
        *error = fail(
            kNoProviderCode,
            sdk::auth::Err::LocalState,
            "auth provider is not configured"
        );
        return false;
    }

    return true;
}

// 确保会话有效，调用方必须持有 mutex_
bool AuthClient::ensure_session_locked(sdk::auth::Result* error) const {
    if (!state_.session.live()) {
        *error = fail(
            kNoSessionCode,
            sdk::auth::Err::LocalState,
            "auth session is not active"
        );
        return false;
    }

    return true;
}

// 填充 provider 调用快照，调用方必须持有 mutex_
bool AuthClient::fill_provider_call_locked(ProviderCall* call, sdk::auth::Result* error) const {
    if (call == nullptr ||
        !ensure_context_locked(error) ||
        !ensure_provider_locked(error)) {
        return false;
    }

    call->context = state_.context;
    call->provider = state_.provider;
    call->generation = state_.generation;
    return true;
}

// 填充会话调用快照，调用方必须持有 mutex_
bool AuthClient::fill_session_call_locked(ProviderCall* call, sdk::auth::Result* error) const {
    if (call == nullptr ||
        !ensure_context_locked(error) ||
        !ensure_session_locked(error) ||
        !ensure_provider_locked(error)) {
        return false;
    }

    call->context = state_.context;
    call->provider = state_.provider;
    call->generation = state_.generation;
    return true;
}

// 判断远程变量请求是否仍匹配当前会话，调用方必须持有 mutex_
bool AuthClient::session_matches_locked(const ContextState& context, const VarInput& input) const {
    if (state_.context != context || !state_.session.live()) {
        return false;
    }

    const SessionInput expected{input.license, input.device_id, input.token};
    return same_session_input(state_.session.session_input(), expected);
}

// 全局唯一鉴权客户端
AuthClient& auth_client() {
    static AuthClient instance;
    return instance;
}

} // namespace

// 判断当前会话是否授权指定功能
bool is_feature_licensed(const std::string& feature) {
    return auth_client().is_feature_licensed(feature);
}

// 导出短生命周期能力票据
sdk::auth::Value<sdk::auth::Capability> export_capability(sdk::auth::CapabilityPurpose purpose) {
    return auth_client().export_capability(purpose);
}

// 校验短生命周期能力票据
bool verify_capability(sdk::auth::CapabilityPurpose purpose, const sdk::auth::Capability& capability) {
    return auth_client().verify_capability(purpose, capability);
}

// 判断鉴权客户端是否已准备好
bool is_ready() {
    return auth_client().is_ready();
}

namespace client {

// 配置鉴权上下文和 provider
sdk::auth::Result configure(const sdk::auth::Context& context, const sdk::auth::InitConfig& config) {
    return auth_client().configure(context, config);
}

// 登录并建立会话
sdk::auth::Result login(const char* license) {
    return auth_client().login(license);
}

// 登出并清理本地会话
void logout() {
    auth_client().logout();
}

// 判断当前是否有有效会话
bool has_session() {
    return auth_client().has_session();
}

// 判断当前会话是否授权指定功能
bool is_feature_licensed(const std::string& feature) {
    return auth_client().is_feature_licensed(feature);
}

// 导出短生命周期能力票据
sdk::auth::Value<sdk::auth::Capability> export_capability(sdk::auth::CapabilityPurpose purpose) {
    return auth_client().export_capability(purpose);
}

// 校验短生命周期能力票据
bool verify_capability(sdk::auth::CapabilityPurpose purpose, const sdk::auth::Capability& capability) {
    return auth_client().verify_capability(purpose, capability);
}

// 判断鉴权客户端是否已准备好
bool is_ready() {
    return auth_client().is_ready();
}

// 获取远程变量
sdk::auth::Result fetch_var(const char* key, char* out, std::size_t out_len) {
    return auth_client().fetch_var(key, out, out_len);
}

// 获取公告
sdk::auth::Result fetch_notice(char* out, std::size_t out_len) {
    return auth_client().fetch_notice(out, out_len);
}

// 检查更新
sdk::auth::Result fetch_update(char* out, std::size_t out_len) {
    return auth_client().fetch_update(out, out_len);
}

// 执行心跳
sdk::auth::Result heartbeat() {
    return auth_client().heartbeat();
}

// 保存当前客户端状态快照
Snapshot save_snapshot() {
    Snapshot snapshot;
    snapshot.state = auth_client().save_snapshot();
    return snapshot;
}

// 恢复客户端状态快照
void restore_snapshot(const Snapshot& snapshot) {
    const auto copy = std::static_pointer_cast<SnapshotState>(snapshot.state);
    auth_client().restore_snapshot(copy.get());
}

// 重置客户端状态
void reset() {
    auth_client().reset();
}

// 切换到无 provider 模式
void use_no_provider() {
    auth_client().use_no_provider();
}

// 切换到 mock provider
void use_mock() {
    auth_client().use_mock();
}

// 设置 mock 心跳失败次数
void set_heartbeat_failures(int count) {
    auth_client().set_heartbeat_failures(count);
}

// 设置当前线程的渲染线程标记
void set_render_thread(bool enabled) {
    auth_client().set_render_thread(enabled);
}

// 获取当前设备 ID
std::string device_id() {
    return auth_client().device_id();
}

// 判断本地会话封存文件是否存在
bool has_session_store() {
    return auth_client().has_session_store();
}

// 获取本地会话封存文件路径
std::string session_store_path() {
    return auth_client().session_store_path();
}

} // namespace client
} // namespace auth
} // namespace core
} // namespace nyx
