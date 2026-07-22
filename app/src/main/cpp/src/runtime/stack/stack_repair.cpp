#include "src/runtime/stack/stack_repair.h"

#include "src/runtime/stack/stack_base.h"

#include <algorithm>
#include <utility>

namespace nyx {
namespace runtime {
namespace stack {

namespace {

// 判断栈帧类型是否允许修复
bool repairable_kind(StackType kind) {
    return kind == StackType::HookReplacement ||
        kind == StackType::HookTrampoline ||
        kind == StackType::PltHub ||
        kind == StackType::VendorHook;
}

// 判断地址是否是可写栈槽
bool writable_stack_slot(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address
) {
    address = data_address(address);
    memory::MemoryMapEntry entry;
    return aligned_pointer(address) &&
        find_map(maps, address, &entry) &&
        stack_entry(entry) &&
        span_in_entry(entry, address, sizeof(std::uintptr_t));
}

// 从栈槽读取一个指针宽度的值
bool read_word(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    std::uintptr_t* out
) {
    if (out == nullptr || !writable_stack_slot(maps, address)) {
        return false;
    }
    *out = *reinterpret_cast<const std::uintptr_t*>(data_address(address));
    return true;
}

// 添加去重后的修复补丁
bool add_patch(
    StackRepairSlot slot,
    std::uintptr_t address,
    std::uintptr_t before,
    std::uintptr_t after,
    const char* reason,
    std::vector<StackRepairPatch>* out
) {
    if (out == nullptr || address == 0 || before == after) {
        return false;
    }

    const auto duplicate = std::find_if(out->begin(), out->end(), [address](const StackRepairPatch& patch) {
        return patch.address == address;
    });
    if (duplicate != out->end()) {
        return false;
    }

    StackRepairPatch patch;
    patch.slot = slot;
    patch.address = address;
    patch.before = before;
    patch.after = after;
    patch.reason = reason != nullptr ? reason : "";
    out->push_back(std::move(patch));
    return true;
}

// 计算保存 FP/RA 的栈槽地址
bool saved_pair_address(
    const NormalizedStackFrame& frame,
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t* fp_slot,
    std::uintptr_t* ra_slot
) {
    const std::uintptr_t fp = data_address(frame.raw.frame_pointer);
    if (fp == 0 ||
        !writable_stack_slot(maps, fp) ||
        !writable_stack_slot(maps, fp + sizeof(std::uintptr_t))) {
        return false;
    }

    if (fp_slot != nullptr) {
        *fp_slot = fp;
    }
    if (ra_slot != nullptr) {
        *ra_slot = fp + sizeof(std::uintptr_t);
    }
    return true;
}

// 判断父帧指针是否仍在同一栈映射内且单调递增
bool valid_parent_frame(
    std::uintptr_t current,
    std::uintptr_t parent,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    current = data_address(current);
    parent = data_address(parent);
    if (current == 0 || parent == 0 || parent <= current) {
        return false;
    }

    memory::MemoryMapEntry current_entry;
    memory::MemoryMapEntry parent_entry;
    return find_map(maps, current, &current_entry) &&
        find_map(maps, parent, &parent_entry) &&
        current_entry.start == parent_entry.start &&
        current_entry.end == parent_entry.end &&
        writable_stack_slot(maps, parent) &&
        writable_stack_slot(maps, parent + sizeof(std::uintptr_t));
}

// 规划保存返回地址的修复
bool plan_return_address(
    const NormalizedStackFrame& frame,
    const std::vector<memory::MemoryMapEntry>& maps,
    std::vector<StackRepairPatch>* out
) {
    if (!repairable_kind(frame.kind)) {
        return false;
    }

    const std::uintptr_t after = code_address(frame.normalized_return_address);
    if (after == 0 || !executable_address(maps, after)) {
        return false;
    }

    std::uintptr_t ra_slot = 0;
    if (!saved_pair_address(frame, maps, nullptr, &ra_slot)) {
        return false;
    }

    std::uintptr_t before = 0;
    if (!read_word(maps, ra_slot, &before)) {
        return false;
    }

    const std::uintptr_t saved = code_address(before);
    const std::uintptr_t raw_pc = code_address(frame.raw.pc);
    const std::uintptr_t raw_ra = code_address(frame.raw.return_address);
    if (saved == after) {
        return false;
    }

    // 只修正能和原始 PC/RA 对上的槽位，避免误写普通栈数据
    if (saved != raw_pc && saved != raw_ra && raw_pc != raw_ra) {
        return false;
    }

    return add_patch(
        StackRepairSlot::SavedReturnAddress,
        ra_slot,
        before,
        after,
        "repair saved return address",
        out
    );
}

// 规划保存父帧指针的修复
bool plan_frame_pointer(
    const NormalizedStackFrame& frame,
    const std::vector<memory::MemoryMapEntry>& maps,
    std::vector<StackRepairPatch>* out
) {
    if (!repairable_kind(frame.kind)) {
        return false;
    }

    const std::uintptr_t after = data_address(frame.normalized_frame_pointer);
    std::uintptr_t fp_slot = 0;
    if (after == 0 || !saved_pair_address(frame, maps, &fp_slot, nullptr)) {
        return false;
    }
    if (!valid_parent_frame(frame.raw.frame_pointer, after, maps)) {
        return false;
    }

    std::uintptr_t before = 0;
    if (!read_word(maps, fp_slot, &before)) {
        return false;
    }

    const std::uintptr_t old_parent = data_address(before);
    if (old_parent == after) {
        return false;
    }

    return add_patch(
        StackRepairSlot::SavedFramePointer,
        fp_slot,
        before,
        after,
        "repair saved frame pointer",
        out
    );
}

} // namespace

// 为归一化栈帧规划可安全写入的修复补丁
RuntimeResult plan_stack_repair(
    const std::vector<NormalizedStackFrame>& frames,
    std::vector<StackRepairPatch>* out
) {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stack repair output"};
    }
    out->clear();
    if (frames.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing normalized stack frames"};
    }

    std::vector<memory::MemoryMapEntry> maps;
    const auto map_result = load_memory_map(&maps);
    if (!map_result.ok()) {
        return map_result;
    }

    for (const auto& frame : frames) {
        static_cast<void>(plan_frame_pointer(frame, maps, out));
        static_cast<void>(plan_return_address(frame, maps, out));
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::Unavailable, "no safely repairable stack slots found"};
    }
    return RuntimeResult{};
}

// 析构时恢复已应用的栈修复
StackRepairScope::~StackRepairScope() {
    restore();
}

// 应用栈修复补丁
RuntimeResult StackRepairScope::apply(
    const std::vector<NormalizedStackFrame>& frames,
    std::vector<StackRepairPatch>* out
) {
    if (!patches_.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "stack repair scope is already active"};
    }

    std::vector<StackRepairPatch> patches;
    const auto planned = plan_stack_repair(frames, &patches);
    if (!planned.ok()) {
        if (out != nullptr) {
            out->clear();
        }
        return planned;
    }

    std::vector<memory::MemoryMapEntry> maps;
    const auto map_result = load_memory_map(&maps);
    if (!map_result.ok()) {
        return map_result;
    }

    // 应用前重新确认槽位内容未被其他路径改写
    for (auto& patch : patches) {
        std::uintptr_t current = 0;
        if (!read_word(maps, patch.address, &current)) {
            restore();
            return RuntimeResult{RuntimeStatus::Denied, "stack repair slot is no longer writable"};
        }
        if (current != patch.before) {
            restore();
            return RuntimeResult{RuntimeStatus::Failed, "stack repair slot changed before apply"};
        }

        *reinterpret_cast<std::uintptr_t*>(patch.address) = patch.after;
        patch.applied = true;
        patches_.push_back(patch);
    }

    if (out != nullptr) {
        *out = patches_;
    }
    return RuntimeResult{};
}

// 恢复已应用的栈修复补丁
void StackRepairScope::restore() {
    if (patches_.empty()) {
        return;
    }

    std::vector<memory::MemoryMapEntry> maps;
    const bool has_maps = load_memory_map(&maps).ok();
    for (auto it = patches_.rbegin(); it != patches_.rend(); ++it) {
        if (!it->applied) {
            continue;
        }
        if (has_maps && !writable_stack_slot(maps, it->address)) {
            continue;
        }
        *reinterpret_cast<std::uintptr_t*>(it->address) = it->before;
        it->applied = false;
    }
    patches_.clear();
}

} // namespace stack
} // namespace runtime
} // namespace nyx
