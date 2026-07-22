#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"
#include "src/runtime/vfs/path_policy.h"

namespace nyx {
namespace runtime {
namespace vfs {

// 路径重定向规则
struct PathRule {
    // 原始路径
    std::string from;
    // 目标路径
    std::string to;
};

// 线程安全的路径映射表
class PathMapper {
public:
    // 使用指定策略创建映射表
    explicit PathMapper(PathPolicy policy);

    // 添加或更新路径规则
    RuntimeResult add(PathRule rule);
    // 移除路径规则
    RuntimeResult remove(const std::string& from);
    // 查询路径映射目标
    RuntimeResult map(const std::string& path, std::string* out) const;
    // 获取当前策略
    PathPolicy policy() const;
    // 获取当前规则列表
    std::vector<PathRule> rules() const;
    // 清空规则列表
    void clear();

private:
    // 路径访问策略
    PathPolicy policy_;
    // 保护规则表的锁
    mutable std::mutex mutex_;
    // 路径映射规则
    std::vector<PathRule> rules_;
};

} // namespace vfs
} // namespace runtime
} // namespace nyx
