#pragma once

#include <string>
#include <vector>

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_protect.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 简化后的页权限
struct PagePermission {
    // 是否可读
    bool read = false;
    // 是否可写
    bool write = false;
    // 是否可执行
    bool execute = false;
};

// 权限转换场景
enum class PermissionTransitionKind {
    // 去掉可写代码页的写权限
    SealWritableCode,
    // 为补丁临时增加写权限
    MakeWritableForPatch,
    // 恢复原权限
    RestoreOriginal,
    // 自定义转换
    Custom,
};

// 单条权限规划结果
struct PermissionPlan {
    // 转换场景
    PermissionTransitionKind kind = PermissionTransitionKind::Custom;
    // 目标映射
    MemoryMapEntry target;
    // 转换前权限
    PagePermission before;
    // 转换后权限
    PagePermission after;
    // 是否允许执行
    bool allowed = false;
    // 规划原因
    std::string reason;
};

// 从 maps 条目提取页权限
PagePermission permission_from(const MemoryMapEntry& entry);
// 从保护请求提取页权限
PagePermission permission_from(const PageProtectRequest& request);
// 构造页保护请求
PageProtectRequest protect_request(
    std::uintptr_t start,
    std::uintptr_t end,
    const PagePermission& permission
);

// 权限规划器
class PermissionPlanner {
public:
    // 为一组映射生成权限规划
    RuntimeResult plan(
        const std::vector<MemoryMapEntry>& entries,
        PermissionTransitionKind kind,
        std::vector<PermissionPlan>* out
    ) const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
