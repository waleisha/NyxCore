#include "src/runtime/vfs/path_mapper.h"

#include <mutex>
#include <utility>

namespace nyx {
namespace runtime {
namespace vfs {

// 使用指定策略创建映射表
PathMapper::PathMapper(PathPolicy policy) : policy_(std::move(policy)) {}

// 添加或更新路径规则
RuntimeResult PathMapper::add(PathRule rule) {
    std::string reason;
    if (!policy_.allows(rule.from, rule.to, &reason)) {
        return RuntimeResult{RuntimeStatus::Denied, reason};
    }

    // 同 source 的规则直接更新目标路径
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& existing : rules_) {
        if (existing.from == rule.from) {
            existing.to = std::move(rule.to);
            return RuntimeResult{};
        }
    }

    rules_.push_back(std::move(rule));
    return RuntimeResult{};
}

// 移除路径规则
RuntimeResult PathMapper::remove(const std::string& from) {
    if (from.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing path rule source"};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = rules_.begin(); it != rules_.end(); ++it) {
        if (it->from == from) {
            rules_.erase(it);
            return RuntimeResult{};
        }
    }

    return RuntimeResult{RuntimeStatus::NotFound, "path rule not found"};
}

// 查询路径映射目标
RuntimeResult PathMapper::map(const std::string& path, std::string* out) const {
    if (path.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing path or output"};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& rule : rules_) {
        if (rule.from == path) {
            *out = rule.to;
            return RuntimeResult{};
        }
    }

    return RuntimeResult{RuntimeStatus::NotFound, "path rule not found"};
}

// 获取当前策略
PathPolicy PathMapper::policy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return policy_;
}

// 获取当前规则列表
std::vector<PathRule> PathMapper::rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

// 清空规则列表
void PathMapper::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.clear();
}

} // namespace vfs
} // namespace runtime
} // namespace nyx
