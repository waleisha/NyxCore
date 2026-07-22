#include "src/runtime/stack/stack_context.h"

#include "src/runtime/stack/stack_base.h"

#include <ucontext.h>

namespace nyx {
namespace runtime {
namespace stack {

namespace {

// 当前 ABI 是否支持帧指针链遍历
bool fp_supported() {
#if defined(__aarch64__) || defined(__arm__)
    return true;
#else
    return false;
#endif
}

// 判断地址是否是当前栈内的可用槽位
bool stack_address(const memory::MemoryMapEntry& stack, std::uintptr_t address, std::size_t size) {
    address = data_address(address);
    if (!aligned_pointer(address)) {
        return false;
    }
    return span_in_entry(stack, address, size);
}

} // namespace

// 读取当前线程的 CPU 栈上下文
RuntimeResult StackContextReader::current(StackCpuContext* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stack context output"};
    }

    StackCpuContext context;
    volatile std::uintptr_t stack_marker = 0;
    // 用编译器内建接口尽量取得当前 PC/SP/FP
    context.pc = code_address(reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
    context.lr = context.pc;
    context.sp = reinterpret_cast<std::uintptr_t>(&stack_marker);
    context.from_builtin = true;
    context.valid = context.pc != 0;
#if defined(__aarch64__) || defined(__arm__) || defined(__i386__) || defined(__x86_64__)
    context.fp = data_address(reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)));
#endif

    *out = context;
    if (!context.valid) {
        return RuntimeResult{RuntimeStatus::Unavailable, "current stack context is unavailable"};
    }
    return RuntimeResult{};
}

// 从 signal ucontext 读取 CPU 栈上下文
RuntimeResult StackContextReader::from_signal_context(void* ucontext, StackCpuContext* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing signal stack context output"};
    }
    *out = StackCpuContext{};
    if (ucontext == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing signal ucontext"};
    }

    const auto* context = static_cast<const ucontext_t*>(ucontext);
    StackCpuContext cpu;
    cpu.from_signal_context = true;
    // Android 常见的 ARM ABI 从 mcontext 中直接取寄存器
#if defined(__aarch64__)
    cpu.fp = data_address(static_cast<std::uintptr_t>(context->uc_mcontext.regs[29]));
    cpu.lr = code_address(static_cast<std::uintptr_t>(context->uc_mcontext.regs[30]));
    cpu.pc = code_address(static_cast<std::uintptr_t>(context->uc_mcontext.pc));
    cpu.sp = data_address(static_cast<std::uintptr_t>(context->uc_mcontext.sp));
    cpu.valid = cpu.pc != 0 || cpu.lr != 0;
#elif defined(__arm__)
    cpu.fp = data_address(static_cast<std::uintptr_t>(context->uc_mcontext.arm_fp));
    cpu.lr = code_address(static_cast<std::uintptr_t>(context->uc_mcontext.arm_lr));
    cpu.pc = code_address(static_cast<std::uintptr_t>(context->uc_mcontext.arm_pc));
    cpu.sp = data_address(static_cast<std::uintptr_t>(context->uc_mcontext.arm_sp));
    cpu.valid = cpu.pc != 0 || cpu.lr != 0;
#else
    static_cast<void>(context);
    return RuntimeResult{RuntimeStatus::Unavailable, "signal stack context is unavailable for this ABI"};
#endif

    *out = cpu;
    if (!cpu.valid) {
        return RuntimeResult{RuntimeStatus::Unavailable, "signal stack context has no PC or LR"};
    }
    return RuntimeResult{};
}

// 按帧指针链遍历保存的返回地址
RuntimeResult FramePointerWalker::walk(
    const StackCpuContext& context,
    std::size_t max_frames,
    std::vector<FramePointerNode>* out
) const {
    if (out == nullptr || max_frames == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing frame pointer output or limit"};
    }
    out->clear();

    if (!fp_supported()) {
        return RuntimeResult{RuntimeStatus::Unavailable, "frame pointer walk is unavailable for this ABI"};
    }
    if (!context.valid || context.fp == 0) {
        return RuntimeResult{RuntimeStatus::Unavailable, "frame pointer context is unavailable"};
    }

    std::vector<memory::MemoryMapEntry> maps;
    const bool has_maps = load_memory_map(&maps).ok();
    memory::MemoryMapEntry stack;
    const bool has_stack = has_maps &&
        mapped_readable(maps, data_address(context.sp), sizeof(std::uintptr_t), &stack) &&
        stack_entry(stack);
    std::uintptr_t fp = data_address(context.fp);
    for (std::size_t i = 0; i < max_frames && fp != 0; ++i) {
        FramePointerNode node;
        node.fp = fp;

        // 有当前栈范围时，先限制 fp 不要跑出活跃栈
        if (has_stack && !stack_address(stack, fp, sizeof(std::uintptr_t) * 2)) {
            node.reason = "frame pointer outside active stack";
            out->push_back(node);
            break;
        }
        // 没有栈范围时，也要求 fp 至少在可读映射内
        if (!aligned_pointer(fp) || !mapped_readable(maps, fp, sizeof(std::uintptr_t) * 2)) {
            node.reason = "frame pointer out of readable map";
            out->push_back(node);
            break;
        }

        const auto* slots = reinterpret_cast<const std::uintptr_t*>(fp);
        node.parent_fp = data_address(slots[0]);
        node.return_address = code_address(slots[1]);

        // 帧指针链必须单调向高地址移动，返回地址也必须可执行
        if (node.parent_fp != 0 && node.parent_fp <= fp) {
            node.reason = "frame pointer chain is not monotonic: fp=" + std::to_string(fp) +
                " parent=" + std::to_string(node.parent_fp);
        } else if (node.parent_fp != 0 && has_stack &&
            !stack_address(stack, node.parent_fp, sizeof(std::uintptr_t) * 2)) {
            node.reason = "parent frame pointer outside active stack: parent=" + std::to_string(node.parent_fp);
        } else if (node.return_address == 0 || !executable_address(maps, node.return_address)) {
            node.reason = "return address is not executable";
        } else {
            node.trusted = true;
            node.reason = "trusted frame pointer node";
        }
        out->push_back(node);

        if (!node.trusted) {
            break;
        }
        fp = node.parent_fp;
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::Unavailable, "frame pointer walk produced no nodes"};
    }
    return RuntimeResult{};
}

} // namespace stack
} // namespace runtime
} // namespace nyx
