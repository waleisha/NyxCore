#include "sdk/include/vfs_patcher.h"

#include "src/runtime/vfs/io_redirector.h"
#include "src/runtime/vfs/path_mapper.h"
#include "sdk/result_bridge.h"

#include <memory>
#include <mutex>

namespace nyx {
namespace sdk {
namespace vfs {

namespace {

// 默认 VFS 私有根目录
constexpr const char* kDefaultPrivateRoot = "/data/data/dev.nyxcore.manager/files";

// 创建默认路径策略
runtime::vfs::PathPolicy default_policy() {
    runtime::vfs::PathPolicy policy;
    policy.private_root = kDefaultPrivateRoot;
    return policy;
}

// VFS SDK 全局状态
struct State {
    // 保护全局状态的锁
    std::mutex mutex;
    // 当前路径策略
    runtime::vfs::PathPolicy policy = default_policy();
    // 当前路径映射器
    std::unique_ptr<runtime::vfs::PathMapper> mapper;
};

// 获取 VFS SDK 全局状态
State& state() {
    static State value;
    return value;
}

// 懒创建路径映射器，调用方需已持有状态锁
runtime::vfs::PathMapper& mapper_locked(State& value) {
    if (!value.mapper) {
        value.mapper = std::make_unique<runtime::vfs::PathMapper>(value.policy);
    }
    return *value.mapper;
}

// runtime 决策转 SDK 决策
Decision to_decision(const runtime::vfs::PathDecision& decision) {
    return Decision{decision.path, decision.target, decision.reason, decision.redirected};
}

} // namespace

// 初始化 VFS 策略
Result Init(const Config& config) {
    if (config.private_root == nullptr || config.private_root[0] == '\0') {
        return Result{Status::InvalidArgument, "missing VFS private root"};
    }
    if (config.allowed_root_count > 0 && config.allowed_roots == nullptr) {
        return Result{Status::InvalidArgument, "missing VFS allowed roots"};
    }

    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    value.policy.private_root = config.private_root;
    value.policy.read_only = config.read_only;
    value.policy.allow_common_roots = config.allow_common_roots;
    value.policy.allowed_roots.clear();
    for (std::size_t i = 0; i < config.allowed_root_count; ++i) {
        const char* root = config.allowed_roots[i];
        if (root != nullptr && root[0] != '\0') {
            value.policy.allowed_roots.emplace_back(root);
        }
    }
    value.mapper = std::make_unique<runtime::vfs::PathMapper>(value.policy);
    return Result{};
}

// 添加路径重定向规则
Result Redirect(const char* from, const char* to) {
    if (from == nullptr || from[0] == '\0' || to == nullptr || to[0] == '\0') {
        return Result{Status::InvalidArgument, "missing VFS redirect path"};
    }

    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    const auto result = mapper_locked(value).add(runtime::vfs::PathRule{from, to});
    return bridge::result_from(result);
}

// 移除路径重定向规则
Result Remove(const char* from) {
    if (from == nullptr || from[0] == '\0') {
        return Result{Status::InvalidArgument, "missing VFS redirect source"};
    }

    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    return bridge::result_from(mapper_locked(value).remove(from));
}

// 获取路径重定向决策
Result GetRedirect(const char* path, int flags, Decision* out) {
    if (path == nullptr || path[0] == '\0' || out == nullptr) {
        return Result{Status::InvalidArgument, "missing VFS decision input"};
    }

    auto& value = state();
    std::lock_guard<std::mutex> lock(value.mutex);
    runtime::vfs::IoRedirector redirector(mapper_locked(value));
    runtime::vfs::PathDecision decision;
    const auto result = redirector.resolve(path, flags, &decision);
    if (!result.ok()) {
        *out = Decision{};
        return bridge::result_from(result);
    }

    *out = to_decision(decision);
    return bridge::result_from(result);
}

} // namespace vfs
} // namespace sdk
} // namespace nyx
