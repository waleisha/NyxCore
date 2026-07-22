#include "src/runtime/stack/stack_normalizer.h"

#include "src/runtime/stack/stack_base.h"
#include "src/runtime/stack/stack_context.h"
#include "src/runtime/stack/stack_repair.h"
#include "src/runtime/stack/stack_trace.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <string>
#include <vector>

#ifndef NYX_TARGET_TAG
#define NYX_TARGET_TAG "demo"
#endif

#ifndef NYX_NATIVE_LIBRARY_NAME
#define NYX_NATIVE_LIBRARY_NAME "lib" NYX_TARGET_TAG ".so"
#endif

namespace nyx {
namespace runtime {
namespace stack {

namespace {

// 栈扫描最多向后扫描的字节数
constexpr std::size_t kMaxStackScanBytes = 256 * 1024;

// 快照 ID 计数器
std::atomic<std::uint64_t>& next_snapshot_id() {
    static std::atomic<std::uint64_t> value{1};
    return value;
}

// 读取单调时钟纳秒时间
std::uint64_t monotonic_time_ns() {
    timespec now{};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ull +
        static_cast<std::uint64_t>(now.tv_nsec);
}

// 从 PC 构造原始栈帧
RawStackFrame raw_from_pc(std::uintptr_t pc) {
    RawStackFrame frame;
    pc = code_address(pc);
    frame.pc = pc;
    frame.return_address = pc;

    const auto info = query_dladdr(pc);
    frame.symbol = info.symbol;
    frame.module_path = info.module_path;
    frame.module_base = info.module_base;
    frame.module_offset = info.module_offset;

    return frame;
}

// 从 unwinder 帧构造原始栈帧
RawStackFrame raw_from_trace(const StackFrame& frame) {
    RawStackFrame raw;
    raw.pc = code_address(frame.pc);
    raw.return_address = raw.pc;
    raw.symbol = frame.symbol;
    raw.module_path = frame.module_path;
    raw.module_base = frame.module_base;
    raw.module_offset = frame.module_offset;
    raw.from_unwind = true;
    return raw;
}

// 根据归一化 PC 补全符号信息
void fill_normalized_symbol(std::uintptr_t pc, NormalizedStackFrame* frame) {
    if (frame == nullptr || pc == 0) {
        return;
    }

    const auto raw = raw_from_pc(pc);
    frame->normalized_symbol = raw.symbol;
    frame->normalized_module_path = raw.module_path;
    frame->normalized_module_base = raw.module_base;
    frame->normalized_module_offset = raw.module_offset;
}

// 判断模块路径是否来自常见第三方 Hook 框架
bool contains_vendor_name(const std::string& path) {
    const std::string value = lower_copy(path);
    return value.find("dobby") != std::string::npos ||
        value.find("bytehook") != std::string::npos ||
        value.find("shadowhook") != std::string::npos;
}

// 判断模块路径是否来自 PLT Hook 框架
bool contains_plt_name(const std::string& path) {
    const std::string value = lower_copy(path);
    return value.find("bytehook") != std::string::npos ||
        value.find("shadowhook") != std::string::npos;
}

// 判断模块是否属于 runtime 自身
bool is_runtime_module(const std::string& path) {
    const std::string value = lower_copy(path);
    return value.find(NYX_NATIVE_LIBRARY_NAME) != std::string::npos ||
        value.find("nyxcore") != std::string::npos;
}

// 判断模块是否属于应用侧
bool is_application_module(const std::string& path) {
    const std::string value = lower_copy(path);
    return value.find("libnyx_test_probe.so") != std::string::npos;
}

// Hook 范围匹配类型
enum class HookMatchKind {
    // 未匹配
    None,
    // 目标函数范围
    Target,
    // 替换函数范围
    Replacement,
    // 跳板范围
    Trampoline,
    // 原始函数范围
    Original,
    // 第三方 hub 范围
    VendorHub,
};

// Hook 匹配结果
struct HookMatch {
    // 命中的记录
    HookFrameRecord record;
    // 命中的范围类型
    HookMatchKind kind = HookMatchKind::None;
};

// 判断地址是否落在清理后的范围内
bool range_contains(AddressRange range, std::uintptr_t address) {
    range.start = code_address(range.start);
    range.end = code_address(range.end);
    return range.contains(code_address(address));
}

// 匹配地址所属的 Hook 范围
HookMatch match_hook(std::uintptr_t address, const std::vector<HookFrameRecord>& hooks) {
    address = code_address(address);
    for (const auto& hook : hooks) {
        if (!hook.installed) {
            continue;
        }
        if (range_contains(hook.replacement_range, address)) {
            return HookMatch{hook, HookMatchKind::Replacement};
        }
        if (range_contains(hook.trampoline_range, address)) {
            return HookMatch{hook, HookMatchKind::Trampoline};
        }
        if (range_contains(hook.original_range, address)) {
            return HookMatch{hook, HookMatchKind::Original};
        }
        if (range_contains(hook.vendor_hub_range, address)) {
            return HookMatch{hook, HookMatchKind::VendorHub};
        }
        if (range_contains(hook.target_range, address)) {
            return HookMatch{hook, HookMatchKind::Target};
        }
    }

    return {};
}

// 根据原始帧和 Hook 命中结果分类栈帧
StackType kind_for(const RawStackFrame& raw, const HookMatch& match) {
    if (raw.from_signal_context) {
        return StackType::SignalHandler;
    }

    switch (match.kind) {
        case HookMatchKind::Replacement:
            return StackType::HookReplacement;
        case HookMatchKind::Trampoline:
        case HookMatchKind::Original:
            return StackType::HookTrampoline;
        case HookMatchKind::VendorHub:
            return match.record.kind == HookType::Plt ? StackType::PltHub : StackType::VendorHook;
        case HookMatchKind::Target:
        case HookMatchKind::None:
            break;
    }

    if (contains_plt_name(raw.module_path)) {
        return StackType::PltHub;
    }
    if (contains_vendor_name(raw.module_path)) {
        return StackType::VendorHook;
    }
    if (is_application_module(raw.module_path)) {
        return StackType::Application;
    }
    if (is_runtime_module(raw.module_path)) {
        return StackType::Runtime;
    }
    if (!raw.module_path.empty()) {
        return StackType::Application;
    }
    return StackType::Unknown;
}

// 获取 Hook 语义上的原始 PC
std::uintptr_t semantic_pc(const HookFrameRecord& hook) {
    if (hook.original_entry != 0) {
        return code_address(hook.original_entry);
    }
    if (hook.target_range.valid()) {
        return code_address(hook.target_range.start);
    }
    if (hook.original_range.valid()) {
        return code_address(hook.original_range.start);
    }
    return 0;
}

// 判断帧指针是否指向可信栈帧
bool frame_pointer_trusted(
    std::uintptr_t fp,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    fp = data_address(fp);
    if (fp == 0) {
        return false;
    }

    memory::MemoryMapEntry entry;
    if (!aligned_pointer(fp) || !find_map(maps, fp, &entry) ||
        !span_in_entry(entry, fp, sizeof(std::uintptr_t) * 2)) {
        return false;
    }
    return stack_entry(entry);
}

// 读取保存的父帧指针和返回地址
bool saved_frame(
    std::uintptr_t fp,
    std::uintptr_t* parent,
    std::uintptr_t* ra,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    fp = data_address(fp);
    if (!frame_pointer_trusted(fp, maps)) {
        return false;
    }

    const auto* slots = reinterpret_cast<const std::uintptr_t*>(fp);
    const std::uintptr_t parent_fp = data_address(slots[0]);
    const std::uintptr_t return_address = code_address(slots[1]);
    if (parent != nullptr) {
        *parent = parent_fp;
    }
    if (ra != nullptr) {
        *ra = return_address;
    }
    return return_address != 0 && executable_address(maps, return_address) &&
        (parent_fp == 0 || parent_fp > fp);
}

// 判断帧指针是否能读出可信返回地址
bool trusted_frame_pointer(std::uintptr_t fp, const std::vector<memory::MemoryMapEntry>& maps) {
    return saved_frame(fp, nullptr, nullptr, maps);
}

// 从保存返回地址的槽位反推出帧指针
std::uintptr_t frame_pointer_from_slot(
    std::uintptr_t slot,
    std::uintptr_t wanted_return,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    slot = data_address(slot);
    wanted_return = code_address(wanted_return);
    if (slot < sizeof(std::uintptr_t) || wanted_return == 0) {
        return 0;
    }

    const std::uintptr_t fp = slot - sizeof(std::uintptr_t);
    std::uintptr_t saved_return = 0;
    if (saved_frame(fp, nullptr, &saved_return, maps) && saved_return == wanted_return) {
        return fp;
    }
    return 0;
}

// 判断原始帧列表中是否已有相同 PC
bool duplicate_raw_pc(const std::vector<RawStackFrame>& frames, std::uintptr_t pc) {
    pc = code_address(pc);
    return std::any_of(frames.begin(), frames.end(), [pc](const RawStackFrame& frame) {
        return code_address(frame.pc) == pc;
    });
}

// 扫描栈内看起来像返回地址的可执行地址
void append_stack_scan_frames(
    const StackCpuContext& context,
    std::size_t max_frames,
    std::vector<RawStackFrame>* frames,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    if (frames == nullptr || max_frames == 0 || frames->size() >= max_frames || !context.valid || context.sp == 0) {
        return;
    }

    const std::uintptr_t sp = data_address(context.sp);
    memory::MemoryMapEntry stack;
    if (!find_map(maps, sp, &stack) || !stack_entry(stack)) {
        return;
    }

    // 从对齐后的 SP 开始，最多扫描当前栈的一小段窗口
    std::uintptr_t slot = sp - (sp % alignof(std::uintptr_t));
    if (slot < stack.start) {
        slot = stack.start;
    }
    std::uintptr_t scan_end = stack.end;
    if (slot <= std::numeric_limits<std::uintptr_t>::max() - kMaxStackScanBytes) {
        scan_end = std::min(scan_end, slot + static_cast<std::uintptr_t>(kMaxStackScanBytes));
    }

    for (; slot + sizeof(std::uintptr_t) <= scan_end && frames->size() < max_frames; slot += sizeof(std::uintptr_t)) {
        std::uintptr_t value = 0;
        if (!readable_slot(maps, slot, &value)) {
            continue;
        }

        const std::uintptr_t pc = code_address(value);
        if (pc == 0 || duplicate_raw_pc(*frames, pc) || !executable_address(maps, pc)) {
            continue;
        }

        RawStackFrame frame = raw_from_pc(pc);
        frame.return_address = pc;
        frame.stack_pointer = slot;
        frame.frame_pointer = frame_pointer_from_slot(slot, pc, maps);
        frame.from_stack_scan = true;
        frames->push_back(std::move(frame));
    }
}

// 判断 Hook 匹配是否是辅助展开帧
bool hook_support_frame(HookMatchKind kind) {
    return kind == HookMatchKind::Trampoline ||
        kind == HookMatchKind::Original ||
        kind == HookMatchKind::VendorHub;
}

// 判断 Hook 匹配是否属于 Hook 栈帧
bool hook_frame(HookMatchKind kind) {
    return kind == HookMatchKind::Replacement || hook_support_frame(kind);
}

// 判断归一化类型是否需要折叠
bool collapse_kind(StackType kind) {
    return kind == StackType::HookTrampoline ||
        kind == StackType::PltHub ||
        kind == StackType::VendorHook;
}

// 判断归一化类型是否属于 Hook 相关帧
bool hook_stack_kind(StackType kind) {
    return kind == StackType::HookReplacement ||
        kind == StackType::HookTrampoline ||
        kind == StackType::PltHub ||
        kind == StackType::VendorHook;
}

// 选取用于恢复调用者的地址
std::uintptr_t call_address(const RawStackFrame& frame) {
    const std::uintptr_t ra = code_address(frame.return_address);
    const std::uintptr_t pc = code_address(frame.pc);
    if (ra != 0 && ra != pc) {
        return ra;
    }
    return pc;
}

// 过滤掉不可执行或仍在 Hook 范围内的地址
std::uintptr_t non_hook_address(
    std::uintptr_t address,
    const std::vector<HookFrameRecord>& hooks,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    address = code_address(address);
    if (address == 0 || !executable_address(maps, address)) {
        return 0;
    }
    const auto match = match_hook(address, hooks);
    return hook_frame(match.kind) ? 0 : address;
}

// 从后续原始帧中寻找非 Hook 调用者地址
std::uintptr_t next_caller_address(
    std::size_t index,
    const std::vector<RawStackFrame>& raw,
    const std::vector<HookFrameRecord>& hooks,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    for (std::size_t i = index + 1; i < raw.size(); ++i) {
        const auto match = match_hook(raw[i].pc, hooks);
        if (hook_frame(match.kind)) {
            continue;
        }

        const std::uintptr_t address = non_hook_address(call_address(raw[i]), hooks, maps);
        if (address != 0) {
            return address;
        }
    }
    return 0;
}

// 恢复 Hook 帧应该呈现给上层的返回地址
std::uintptr_t recovered_return_address(
    std::size_t index,
    const std::vector<RawStackFrame>& raw,
    const std::vector<HookFrameRecord>& hooks,
    const HookFrameRecord& hook,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    // 注册表中明确给出的调用点优先
    if (hook.expected_call_site != 0) {
        const std::uintptr_t expected = non_hook_address(hook.expected_call_site, hooks, maps);
        if (expected != 0) {
            return expected;
        }
    }

    const auto& frame = raw[index];
    // 其次使用原始帧自带的返回地址
    const std::uintptr_t direct = non_hook_address(frame.return_address, hooks, maps);
    if (direct != 0 && direct != code_address(frame.pc)) {
        return direct;
    }

    std::uintptr_t saved_return = 0;
    // 再尝试从帧指针保存槽中读取返回地址
    if (saved_frame(frame.frame_pointer, nullptr, &saved_return, maps)) {
        const std::uintptr_t saved = non_hook_address(saved_return, hooks, maps);
        if (saved != 0) {
            return saved;
        }
    }

    // 最后从后续帧中找第一个非 Hook 调用者
    return next_caller_address(index, raw, hooks, maps);
}

// 追加归一化原因
void append_reason(NormalizedStackFrame* frame, const std::string& reason) {
    if (frame == nullptr || reason.empty()) {
        return;
    }
    if (frame->reason.empty() || frame->reason == "raw frame unchanged") {
        frame->reason = reason;
        return;
    }
    frame->reason += "; ";
    frame->reason += reason;
}

// 更新状态，已有状态时只追加原因
void note_status(NormalizedStackFrame* frame, StackStatus status, const std::string& reason) {
    if (frame == nullptr) {
        return;
    }
    if (frame->status == StackStatus::Unchanged) {
        frame->status = status;
        frame->reason = reason;
        return;
    }
    append_reason(frame, reason);
}

// 从相邻帧或栈扫描槽恢复帧指针
std::uintptr_t recovered_frame_pointer(
    std::size_t index,
    const std::vector<NormalizedStackFrame>& frames,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    if (index >= frames.size()) {
        return 0;
    }

    const auto& frame = frames[index];
    const bool hook_frame = hook_stack_kind(frame.kind);
    // 非 Hook 帧优先相信自身可信 FP
    if (!hook_frame && trusted_frame_pointer(frame.raw.frame_pointer, maps)) {
        return frame.raw.frame_pointer;
    }

    const std::uintptr_t wanted_return = frame.normalized_return_address;
    // 扫描帧记录了栈槽地址时，可由保存 RA 的槽位反推 FP
    if (wanted_return != 0 && frame.raw.stack_pointer != 0) {
        const std::uintptr_t recovered = frame_pointer_from_slot(frame.raw.stack_pointer, wanted_return, maps);
        if (recovered != 0) {
            return recovered;
        }
    }

    // 向后寻找能对上目标返回地址的可信帧
    if (wanted_return != 0) {
        for (std::size_t i = index + 1; i < frames.size(); ++i) {
            const auto& candidate = frames[i].raw;
            if (trusted_frame_pointer(candidate.frame_pointer, maps) &&
                (code_address(candidate.return_address) == wanted_return || code_address(candidate.pc) == wanted_return)) {
                return candidate.frame_pointer;
            }
            if (candidate.stack_pointer != 0) {
                const std::uintptr_t recovered = frame_pointer_from_slot(candidate.stack_pointer, wanted_return, maps);
                if (recovered != 0) {
                    return recovered;
                }
            }
        }
    }

    // 找不到精确匹配时，向后借用第一个可信 FP
    for (std::size_t i = index + 1; i < frames.size(); ++i) {
        if (trusted_frame_pointer(frames[i].raw.frame_pointer, maps)) {
            return frames[i].raw.frame_pointer;
        }
    }

    // 后续没有可信 FP 时，向前借用最近的可信 FP
    for (std::size_t i = index; i > 0; --i) {
        if (trusted_frame_pointer(frames[i - 1].raw.frame_pointer, maps)) {
            return frames[i - 1].raw.frame_pointer;
        }
    }

    // Hook 帧自身保存槽可信时使用其父 FP
    if (hook_frame) {
        std::uintptr_t parent = 0;
        if (saved_frame(frame.raw.frame_pointer, &parent, nullptr, maps) && parent != 0) {
            return parent;
        }
    }

    // 最后退回自身可信 FP
    if (trusted_frame_pointer(frame.raw.frame_pointer, maps)) {
        return frame.raw.frame_pointer;
    }

    return 0;
}

// 对所有归一化帧应用帧指针恢复
void apply_frame_pointer_recovery(
    std::vector<NormalizedStackFrame>* frames,
    const std::vector<memory::MemoryMapEntry>& maps
) {
    if (frames == nullptr) {
        return;
    }

    for (std::size_t i = 0; i < frames->size(); ++i) {
        auto& frame = (*frames)[i];
        const bool has_fp = frame.raw.frame_pointer != 0;
        const bool trusted = trusted_frame_pointer(frame.raw.frame_pointer, maps);
        const bool hook_frame = hook_stack_kind(frame.kind);
        if (trusted && !hook_frame) {
            frame.normalized_frame_pointer = frame.raw.frame_pointer;
            if (frame.raw.from_frame_chain) {
                note_status(&frame, StackStatus::RecoveredFramePointer, "trusted frame pointer node");
            }
            continue;
        }

        const std::uintptr_t recovered = recovered_frame_pointer(i, *frames, maps);
        if (recovered != 0 && (!hook_frame || recovered != frame.raw.frame_pointer)) {
            frame.normalized_frame_pointer = recovered;
            note_status(&frame, StackStatus::RecoveredFramePointer, "recovered frame pointer from adjacent chain");
            continue;
        }

        if (trusted) {
            frame.normalized_frame_pointer = frame.raw.frame_pointer;
            if (frame.raw.from_frame_chain) {
                note_status(&frame, StackStatus::RecoveredFramePointer, "trusted frame pointer node");
            }
            continue;
        }

        if (has_fp) {
            note_status(&frame, StackStatus::UntrustedFramePointer, "frame pointer out of stack range");
        }
    }
}

// 折叠 Hook 支撑帧到前一帧的 reason 中
void collapse_hook_frames(std::vector<NormalizedStackFrame>* frames) {
    if (frames == nullptr || frames->empty()) {
        return;
    }

    std::vector<NormalizedStackFrame> collapsed;
    collapsed.reserve(frames->size());
    for (auto& frame : *frames) {
        if (!collapsed.empty() && collapse_kind(frame.kind)) {
            append_reason(
                &collapsed.back(),
                "collapsed hook support frame at raw pc " + std::to_string(frame.raw.pc)
            );
            continue;
        }

        collapsed.push_back(std::move(frame));
    }
    *frames = std::move(collapsed);
}

// 追加帧指针链采集到的原始帧
void append_frame_pointer_frames(
    const StackCpuContext& context,
    std::size_t max_frames,
    std::vector<RawStackFrame>* frames
) {
    if (frames == nullptr || frames->size() >= max_frames) {
        return;
    }

    FramePointerWalker walker;
    std::vector<FramePointerNode> nodes;
    const auto walked = walker.walk(context, max_frames, &nodes);
    if (!walked.ok() && nodes.empty()) {
        return;
    }

    for (const auto& node : nodes) {
        if (frames->size() >= max_frames ||
            node.return_address == 0 ||
            duplicate_raw_pc(*frames, node.return_address)) {
            continue;
        }

        RawStackFrame fp_frame = raw_from_pc(node.return_address);
        fp_frame.return_address = code_address(node.return_address);
        fp_frame.frame_pointer = node.fp;
        fp_frame.parent_frame_pointer = node.parent_fp;
        fp_frame.stack_pointer = node.fp;
        fp_frame.from_frame_chain = true;
        frames->push_back(std::move(fp_frame));
    }
}

// 追加 unwinder 采集到的原始帧
void append_unwind_frames(
    std::size_t max_frames,
    std::vector<RawStackFrame>* frames,
    RuntimeResult* result
) {
    if (frames == nullptr || frames->size() >= max_frames) {
        return;
    }

    StackTrace stack_trace;
    std::vector<StackFrame> unwind_frames;
    const auto unwind = stack_trace.capture(&unwind_frames, max_frames);
    if (result != nullptr) {
        *result = unwind;
    }
    if (!unwind.ok()) {
        return;
    }

    frames->reserve(frames->size() + unwind_frames.size());
    for (const auto& frame : unwind_frames) {
        if (frames->size() >= max_frames) {
            break;
        }
        if (!duplicate_raw_pc(*frames, frame.pc)) {
            frames->push_back(raw_from_trace(frame));
        }
    }
}

// 追加栈扫描发现的原始帧
void append_scan_frames(
    const StackCpuContext& context,
    std::size_t max_frames,
    std::vector<RawStackFrame>* frames
) {
    std::vector<memory::MemoryMapEntry> maps;
    if (load_memory_map(&maps).ok()) {
        append_stack_scan_frames(context, max_frames, frames, maps);
    }
}

} // namespace

// 采集并归一化当前或 signal 上下文栈快照
RuntimeResult StackNormalizer::capture(const StackNormalizeRequest& request, StackTraceSnapshot* out) const {
    if (out == nullptr || request.max_frames == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stack snapshot output or frame limit"};
    }

    StackTraceSnapshot snapshot;
    snapshot.id = next_snapshot_id().fetch_add(1);
    snapshot.monotonic_time_ns = monotonic_time_ns();

    StackContextReader context_reader;
    StackCpuContext context;
    RuntimeResult context_result;
    const bool signal_capture = request.signal_ucontext != nullptr;
    // signal ucontext 优先使用崩溃现场寄存器
    if (request.signal_ucontext != nullptr) {
        context_result = context_reader.from_signal_context(request.signal_ucontext, &context);
        if (context_result.ok()) {
            RawStackFrame signal_frame = raw_from_pc(context.pc != 0 ? context.pc : context.lr);
            signal_frame.return_address = code_address(context.lr);
            signal_frame.frame_pointer = context.fp;
            signal_frame.stack_pointer = context.sp;
            signal_frame.from_signal_context = true;
            snapshot.raw_frames.push_back(signal_frame);
        }
    } else {
        // 普通采集从当前线程上下文开始
        context_result = context_reader.current(&context);
        if (context_result.ok()) {
            RawStackFrame current_frame = raw_from_pc(context.pc != 0 ? context.pc : context.lr);
            current_frame.return_address = code_address(context.lr);
            current_frame.frame_pointer = context.fp;
            current_frame.stack_pointer = context.sp;
            snapshot.raw_frames.push_back(current_frame);
        }
    }

    // 帧指针链能补上 unwinder 可能漏掉的 Hook 前后帧
    if (request.recover_frame_pointer && context_result.ok()) {
        append_frame_pointer_frames(context, request.max_frames, &snapshot.raw_frames);
    }

    // 栈扫描用于补救返回地址恢复
    if (request.recover_return_address && context_result.ok()) {
        append_scan_frames(context, request.max_frames, &snapshot.raw_frames);
    }

    StackHookRegistry registry;
    auto hooks = registry.records();

    // live stack 修复只在非 signal 采集或 signal 上下文不可用时辅助 unwinder
    if (!signal_capture || !context_result.ok()) {
        RuntimeResult unwind;
        if (request.repair_live_stack && !snapshot.raw_frames.empty()) {
            std::vector<NormalizedStackFrame> repair_frames;
            StackNormalizeRequest repair_request = request;
            repair_request.collapse_hook_frames = false;
            repair_request.repair_live_stack = false;
            if (normalize(snapshot.raw_frames, hooks, repair_request, &repair_frames).ok()) {
                StackRepairScope repair_scope;
                static_cast<void>(repair_scope.apply(repair_frames));
                append_unwind_frames(request.max_frames, &snapshot.raw_frames, &unwind);
            } else {
                append_unwind_frames(request.max_frames, &snapshot.raw_frames, &unwind);
            }
        } else {
            append_unwind_frames(request.max_frames, &snapshot.raw_frames, &unwind);
        }
        if (!unwind.ok() && snapshot.raw_frames.empty()) {
            return unwind;
        }
    }

    if (snapshot.raw_frames.size() > request.max_frames) {
        snapshot.raw_frames.resize(request.max_frames);
    }

    const auto normalized = normalize(snapshot.raw_frames, hooks, request, &snapshot.normalized_frames);
    if (!normalized.ok()) {
        return normalized;
    }

    if (!request.include_raw) {
        snapshot.raw_frames.clear();
    }

    *out = std::move(snapshot);
    return RuntimeResult{};
}

// 使用默认请求归一化原始帧
RuntimeResult StackNormalizer::normalize(
    const std::vector<RawStackFrame>& raw,
    const std::vector<HookFrameRecord>& hooks,
    std::vector<NormalizedStackFrame>* out
) const {
    StackNormalizeRequest request;
    request.collapse_hook_frames = false;
    return normalize(raw, hooks, request, out);
}

// 按请求选项归一化原始帧
RuntimeResult StackNormalizer::normalize(
    const std::vector<RawStackFrame>& raw,
    const std::vector<HookFrameRecord>& hooks,
    const StackNormalizeRequest& request,
    std::vector<NormalizedStackFrame>* out
) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing normalized frame output"};
    }
    out->clear();
    if (raw.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing raw stack frames"};
    }

    std::vector<memory::MemoryMapEntry> maps;
    const bool has_maps = load_memory_map(&maps).ok();

    out->reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto& raw_frame = raw[i];
        NormalizedStackFrame frame;
        frame.raw = raw_frame;
        frame.raw.pc = code_address(frame.raw.pc);
        frame.raw.return_address = code_address(frame.raw.return_address);
        frame.raw.frame_pointer = data_address(frame.raw.frame_pointer);
        frame.raw.parent_frame_pointer = data_address(frame.raw.parent_frame_pointer);
        frame.raw.stack_pointer = data_address(frame.raw.stack_pointer);
        frame.normalized_pc = frame.raw.pc;
        frame.normalized_return_address = frame.raw.return_address;
        frame.normalized_frame_pointer = frame.raw.frame_pointer;
        frame.normalized_symbol = raw_frame.symbol;
        frame.normalized_module_path = raw_frame.module_path;
        frame.normalized_module_base = raw_frame.module_base;
        frame.normalized_module_offset = raw_frame.module_offset;
        frame.reason = "raw frame unchanged";

        // maps 不可用或 PC 未映射时，先标记为不可映射地址
        memory::MemoryMapEntry map_entry;
        if (!has_maps || frame.raw.pc == 0 || !find_map(maps, frame.raw.pc, &map_entry)) {
            frame.status = StackStatus::UnmappedAddress;
            frame.reason = "pc is not mapped";
        }

        const auto hook_match = match_hook(frame.raw.pc, hooks);
        frame.kind = kind_for(frame.raw, hook_match);

        if (frame.status != StackStatus::UnmappedAddress) {
            // 替换函数帧归一化回原函数入口，并尝试恢复真实返回地址
            if (hook_match.kind == HookMatchKind::Replacement) {
                const std::uintptr_t pc = semantic_pc(hook_match.record);
                if (request.recover_return_address && pc != 0) {
                    frame.normalized_pc = pc;
                    fill_normalized_symbol(pc, &frame);
                }
                if (request.recover_return_address) {
                    const std::uintptr_t ra = recovered_return_address(i, raw, hooks, hook_match.record, maps);
                    if (ra != 0) {
                        frame.normalized_return_address = ra;
                    }
                    frame.status = StackStatus::AdjustedReturnAddress;
                    frame.reason = hook_match.record.kind == HookType::Plt
                        ? "matched PLT replacement"
                        : "matched inline replacement";
                }
            } else if (hook_match.kind == HookMatchKind::Trampoline || hook_match.kind == HookMatchKind::Original) {
                // 跳板/原函数桥接帧可折叠，也可保留为调整后的帧
                const std::uintptr_t pc = semantic_pc(hook_match.record);
                if (request.recover_return_address && pc != 0) {
                    frame.normalized_pc = pc;
                    fill_normalized_symbol(pc, &frame);
                    const std::uintptr_t ra = recovered_return_address(i, raw, hooks, hook_match.record, maps);
                    if (ra != 0) {
                        frame.normalized_return_address = ra;
                    }
                    frame.status = request.collapse_hook_frames
                        ? StackStatus::CollapsedHookFrame
                        : StackStatus::AdjustedReturnAddress;
                    frame.reason = "matched hook trampoline";
                }
            } else if (hook_match.kind == HookMatchKind::VendorHub) {
                // 第三方 Hook hub 默认作为辅助帧折叠
                frame.status = StackStatus::CollapsedHookFrame;
                frame.reason = "matched vendor hook hub";
                if (request.recover_return_address) {
                    const std::uintptr_t ra = recovered_return_address(i, raw, hooks, hook_match.record, maps);
                    if (ra != 0) {
                        frame.normalized_return_address = ra;
                    }
                }
            }
        }

        out->push_back(std::move(frame));
    }

    if (request.recover_frame_pointer) {
        apply_frame_pointer_recovery(out, maps);
    }
    if (request.collapse_hook_frames) {
        collapse_hook_frames(out);
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::Failed, "normalization produced no frames"};
    }
    return RuntimeResult{};
}

} // namespace stack
} // namespace runtime
} // namespace nyx
