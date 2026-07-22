#include "src/runtime/hook/hook_registry.h"

#include "sdk/include/utils.h"
#include "src/runtime/hook/inline_backend.h"
#include "src/runtime/hook/plt_backend.h"
#include "src/runtime/stack/stack_hooks.h"

#include <cstdint>
#include <utility>

namespace nyx {
namespace runtime {
namespace hook {

namespace {

// 判断两条 hook 请求是否指向同一个后端安装目标
bool same_hook(const HookRecord& lhs, const HookRecord& rhs) {
    return lhs.kind == rhs.kind &&
        lhs.target_address == rhs.target_address &&
        lhs.replacement == rhs.replacement &&
        lhs.caller_path == rhs.caller_path &&
        lhs.callee_path == rhs.callee_path &&
        lhs.symbol == rhs.symbol;
}

// 根据安装结果决定记录状态
HookState state_after_install(const RuntimeResult& result) {
    if (result.ok()) {
        return HookState::Installed;
    }

    return result.status == RuntimeStatus::Failed ? HookState::Failed : HookState::Registered;
}

// 从地址和估算大小生成 stack 识别范围
stack::AddressRange range_from(void* address, std::uintptr_t size, bool estimated = true) {
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (start == 0 || size == 0 || start + size <= start) {
        return {};
    }

    return stack::AddressRange{start, start + size, estimated};
}

// 转换 hook 类型给 stack 模块
stack::HookType stack_kind(HookKind kind) {
    return kind == HookKind::Plt ? stack::HookType::Plt : stack::HookType::Inline;
}

// replacement/trampoline 的无栈帧展开规则
stack::StackUnwindRule frameless_unwind_rule() {
    stack::StackUnwindRule rule;
    rule.kind = stack::StackUnwindRuleKind::Frameless;
    rule.parent_frame_offset = 0;
    rule.return_address_offset = 0;
    rule.stack_delta = 0;
    rule.complete = true;
    return rule;
}

// replacement 常见帧指针成对保存规则
stack::StackUnwindRule frame_pair_unwind_rule() {
    stack::StackUnwindRule rule;
    rule.kind = stack::StackUnwindRuleKind::FramePointerPair;
    rule.parent_frame_offset = 0;
    rule.return_address_offset = static_cast<std::intptr_t>(sizeof(std::uintptr_t));
    rule.stack_delta = 0;
    rule.complete = true;
    return rule;
}

// 将 hook 记录转换为 stack 修复模块可用的帧记录
stack::HookFrameRecord stack_record_from(const HookRecord& record) {
    constexpr std::uintptr_t kTargetRangeSize = 32;
    constexpr std::uintptr_t kReplacementRangeSize = 256;
    constexpr std::uintptr_t kOriginalRangeSize = 64;

    stack::HookFrameRecord out;
    out.owner = record.owner;
    out.target = record.target;
    out.kind = stack_kind(record.kind);
    out.target_range = range_from(record.target_address, kTargetRangeSize);
    out.replacement_range = range_from(record.replacement, kReplacementRangeSize);
    out.trampoline_range = range_from(record.original, kOriginalRangeSize);
    out.original_range = range_from(record.original, kOriginalRangeSize);
    out.original_entry = record.kind == HookKind::Inline
        ? reinterpret_cast<std::uintptr_t>(record.target_address)
        : reinterpret_cast<std::uintptr_t>(record.original);
    out.replacement_entry = reinterpret_cast<std::uintptr_t>(record.replacement);
    out.replacement_unwind = frame_pair_unwind_rule();
    out.trampoline_unwind = frameless_unwind_rule();
    out.original_unwind = frameless_unwind_rule();
    out.installed = record.state == HookState::Installed;
    return out;
}

// 根据 hook 类型选择后端
HookBackend& backend_for(HookKind kind) {
    return kind == HookKind::Plt ? plt_backend() : inline_backend();
}

// 同步已安装 hook 到 stack 记录表
void sync_stack_record(const HookRecord& record) {
    stack::StackHookRegistry registry;
    static_cast<void>(registry.add_or_update(stack_record_from(record)));
}

// 标记 stack 记录已移除
void mark_stack_record_removed(const HookRecord& record) {
    stack::StackHookRegistry registry;
    static_cast<void>(registry.mark_removed(record.owner, record.target));
}

} // namespace

// 添加或替换未安装记录，拒绝和已安装 hook 冲突的请求
bool HookRegistry::add(HookRecord record) {
    if (record.owner.empty() || record.target.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    HookRecord* existing = find_locked(record.owner, record.target);
    if (existing != nullptr) {
        if (!same_hook(*existing, record)) {
            if (existing->state == HookState::Installed) {
                return false;
            }

            record.state = HookState::Registered;
            *existing = std::move(record);
            return true;
        }

        return true;
    }

    record.state = HookState::Registered;
    records_.push_back(std::move(record));
    return true;
}

// 通过指定后端安装已登记记录
RuntimeResult HookRegistry::install(
    const std::string& owner,
    const std::string& target,
    HookBackend& backend
) {
    std::lock_guard<std::mutex> lock(mutex_);
    HookRecord* record = find_locked(owner, target);
    if (record == nullptr) {
        return RuntimeResult{RuntimeStatus::NotFound, "hook record not found"};
    }
    if (record->state == HookState::Installed) {
        sync_stack_record(*record);
        return RuntimeResult{};
    }

    RuntimeResult result = backend.install(*record);
    record->result = result;
    record->state = state_after_install(result);
    if (record->state == HookState::Installed) {
        sync_stack_record(*record);
    }
    return result;
}

// 通过指定后端移除已安装记录
RuntimeResult HookRegistry::remove(
    const std::string& owner,
    const std::string& target,
    HookBackend& backend
) {
    std::lock_guard<std::mutex> lock(mutex_);
    HookRecord* record = find_locked(owner, target);
    if (record == nullptr) {
        return RuntimeResult{RuntimeStatus::NotFound, "hook record not found"};
    }
    if (record->state == HookState::Removed) {
        return RuntimeResult{};
    }
    if (record->state != HookState::Installed) {
        record->result = RuntimeResult{RuntimeStatus::InvalidArgument, "hook is not installed"};
        return record->result;
    }

    const HookState previous_state = record->state;
    RuntimeResult result = backend.remove(*record);
    record->result = result;
    record->state = result.ok() ? HookState::Removed : previous_state;
    if (result.ok()) {
        mark_stack_record_removed(*record);
    }
    return result;
}

// 获取全部记录快照
std::vector<HookRecord> HookRegistry::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

// 获取单条记录快照
std::optional<HookRecord> HookRegistry::find(
    const std::string& owner,
    const std::string& target
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const HookRecord* record = find_locked(owner, target);
    if (record == nullptr) {
        return std::nullopt;
    }

    return *record;
}

// 尽力移除已安装 hook，然后清空注册表
void HookRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        if (record.state != HookState::Installed) {
            continue;
        }

        const RuntimeResult result = backend_for(record.kind).remove(record);
        record.result = result;
        if (result.ok()) {
            record.state = HookState::Removed;
            mark_stack_record_removed(record);
            continue;
        }

        NYX_LOGW(
            "hook clear failed to remove %s/%s: %s",
            record.owner.c_str(),
            record.target.c_str(),
            result.detail.c_str()
        );
    }
    records_.clear();
}

// 查找记录，调用方必须持有 mutex_
HookRecord* HookRegistry::find_locked(const std::string& owner, const std::string& target) {
    for (auto& record : records_) {
        if (record.owner == owner && record.target == target) {
            return &record;
        }
    }
    return nullptr;
}

// 查找记录，调用方必须持有 mutex_
const HookRecord* HookRegistry::find_locked(const std::string& owner, const std::string& target) const {
    for (const auto& record : records_) {
        if (record.owner == owner && record.target == target) {
            return &record;
        }
    }
    return nullptr;
}

} // namespace hook
} // namespace runtime
} // namespace nyx
