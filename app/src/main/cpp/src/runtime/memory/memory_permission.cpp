#include "src/runtime/memory/memory_permission.h"

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 为单条 maps 记录生成权限转换规划
PermissionPlan plan_entry(const MemoryMapEntry& entry, PermissionTransitionKind kind) {
    PermissionPlan plan;
    plan.kind = kind;
    plan.target = entry;
    plan.before = permission_from(entry);
    plan.after = plan.before;

    switch (kind) {
        case PermissionTransitionKind::SealWritableCode:
            if (plan.before.write && plan.before.execute) {
                plan.after.write = false;
                plan.allowed = true;
                plan.reason = "writable executable page can be sealed";
            } else {
                plan.reason = "entry is not writable executable";
            }
            break;
        case PermissionTransitionKind::MakeWritableForPatch:
            if (entry.readable()) {
                plan.after.read = true;
                plan.after.write = true;
                plan.allowed = true;
                plan.reason = "readable page can be made writable for patching";
            } else {
                plan.reason = "entry is not readable";
            }
            break;
        case PermissionTransitionKind::RestoreOriginal:
            plan.allowed = true;
            plan.reason = "original permissions are already represented by the entry";
            break;
        case PermissionTransitionKind::Custom:
            plan.reason = "custom permission transitions require an explicit request";
            break;
    }

    return plan;
}

} // namespace

// 从 maps 条目提取页权限
PagePermission permission_from(const MemoryMapEntry& entry) {
    return PagePermission{entry.readable(), entry.writable(), entry.executable()};
}

// 从保护请求提取页权限
PagePermission permission_from(const PageProtectRequest& request) {
    return PagePermission{request.read, request.write, request.execute};
}

// 构造页保护请求
PageProtectRequest protect_request(
    std::uintptr_t start,
    std::uintptr_t end,
    const PagePermission& permission
) {
    return PageProtectRequest{start, end, permission.read, permission.write, permission.execute};
}

// 为一组映射生成权限规划
RuntimeResult PermissionPlanner::plan(
    const std::vector<MemoryMapEntry>& entries,
    PermissionTransitionKind kind,
    std::vector<PermissionPlan>* out
) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing permission plan output"};
    }
    out->clear();
    out->reserve(entries.size());

    for (const auto& entry : entries) {
        out->push_back(plan_entry(entry, kind));
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
