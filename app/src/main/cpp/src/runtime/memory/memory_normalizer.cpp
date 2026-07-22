#include "src/runtime/memory/memory_normalizer.h"

#include <string>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

// 捕获当前 VMA 快照
RuntimeResult MemoryNormalizer::snapshot(VmaSnapshot* out) const {
    VmaSnapshotter snapshotter;
    return snapshotter.capture(out);
}

// 对比两个 VMA 快照
RuntimeResult MemoryNormalizer::diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out) const {
    VmaSnapshotter snapshotter;
    return snapshotter.diff(before, after, out);
}

// 生成权限变更计划
RuntimeResult MemoryNormalizer::plan_permissions(
    const std::vector<MemoryMapEntry>& entries,
    PermissionTransitionKind kind,
    std::vector<PermissionPlan>* out
) const {
    PermissionPlanner planner;
    return planner.plan(entries, kind, out);
}

// 提交 VMA 事务
RuntimeResult MemoryNormalizer::apply(VmaTransaction* transaction, std::vector<VmaOperationRecord>* out) const {
    if (transaction == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma transaction"};
    }

    return transaction->commit(out);
}

// 回滚已提交的 VMA 事务
RuntimeResult MemoryNormalizer::rollback(std::uint64_t transaction_id, std::vector<VmaOperationRecord>* out) const {
    return VmaTransaction::rollback_committed(transaction_id, out);
}

// 将库代码段收敛到计划权限
RuntimeResult MemoryNormalizer::protect_library_code(
    const std::string& name,
    std::vector<VmaOperationRecord>* out
) const {
    if (name.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing library name or vma operation output"};
    }
    out->clear();

    MemoryMap memory_map;
    std::vector<MemoryMapEntry> entries;
    auto found = memory_map.find_library(name, &entries);
    if (!found.ok()) {
        return found;
    }

    std::vector<PermissionPlan> plans;
    auto planned = plan_permissions(entries, PermissionTransitionKind::SealWritableCode, &plans);
    if (!planned.ok()) {
        return planned;
    }

    VmaTransaction transaction;
    for (const auto& plan : plans) {
        if (!plan.allowed) {
            continue;
        }

        auto added = transaction.add_protect(protect_request(plan.target.start, plan.target.end, plan.after));
        if (!added.ok()) {
            return added;
        }
    }

    return transaction.commit(out);
}

} // namespace memory
} // namespace runtime
} // namespace nyx
