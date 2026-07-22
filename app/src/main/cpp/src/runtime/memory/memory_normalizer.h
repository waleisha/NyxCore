#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/memory/memory_snapshot.h"
#include "src/runtime/memory/memory_permission.h"
#include "src/runtime/memory/memory_transaction.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// VMA 归一化门面：快照、diff、权限规划和事务执行
class MemoryNormalizer {
public:
    // 采集当前 VMA 快照
    RuntimeResult snapshot(VmaSnapshot* out) const;
    // 比较两份 VMA 快照
    RuntimeResult diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out) const;
    // 生成权限规划
    RuntimeResult plan_permissions(
        const std::vector<MemoryMapEntry>& entries,
        PermissionTransitionKind kind,
        std::vector<PermissionPlan>* out
    ) const;
    // 提交 VMA 事务
    RuntimeResult apply(VmaTransaction* transaction, std::vector<VmaOperationRecord>* out) const;
    // 回滚已提交事务
    RuntimeResult rollback(std::uint64_t transaction_id, std::vector<VmaOperationRecord>* out) const;
    // 封住指定库中可写代码页的写权限
    RuntimeResult protect_library_code(const std::string& name, std::vector<VmaOperationRecord>* out) const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
