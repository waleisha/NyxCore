#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nyx {
namespace runtime {
namespace stack {

// 归一化后的栈帧类型
enum class StackType {
    // 未识别类型
    Unknown,
    // 应用模块帧
    Application,
    // Nyx/runtime 内部帧
    Runtime,
    // Hook 替换函数帧
    HookReplacement,
    // Hook 跳板或原函数桥接帧
    HookTrampoline,
    // PLT Hook 框架中转帧
    PltHub,
    // 第三方 Hook 框架帧
    VendorHook,
    // 信号处理上下文帧
    SignalHandler,
};

// 栈帧归一化状态
enum class StackStatus {
    // 未调整
    Unchanged,
    // 已修正返回地址
    AdjustedReturnAddress,
    // 已折叠 Hook 辅助帧
    CollapsedHookFrame,
    // 已恢复帧指针
    RecoveredFramePointer,
    // 帧指针不可信
    UntrustedFramePointer,
    // PC 未映射
    UnmappedAddress,
    // 处理失败
    Failed,
};

// 原始栈帧
struct RawStackFrame {
    // 程序计数器
    std::uintptr_t pc = 0;
    // 返回地址
    std::uintptr_t return_address = 0;
    // 帧指针
    std::uintptr_t frame_pointer = 0;
    // 父帧指针
    std::uintptr_t parent_frame_pointer = 0;
    // 栈指针或扫描槽地址
    std::uintptr_t stack_pointer = 0;
    // 符号名
    std::string symbol;
    // 模块路径
    std::string module_path;
    // 模块基址
    std::uintptr_t module_base = 0;
    // 模块偏移
    std::uintptr_t module_offset = 0;
    // 是否来自 unwinder
    bool from_unwind = false;
    // 是否来自帧指针链
    bool from_frame_chain = false;
    // 是否来自栈扫描
    bool from_stack_scan = false;
    // 是否来自 signal ucontext
    bool from_signal_context = false;
};

// 归一化栈帧
struct NormalizedStackFrame {
    // 原始帧
    RawStackFrame raw;
    // 归一化类型
    StackType kind = StackType::Unknown;
    // 归一化后的 PC
    std::uintptr_t normalized_pc = 0;
    // 归一化后的返回地址
    std::uintptr_t normalized_return_address = 0;
    // 归一化后的帧指针
    std::uintptr_t normalized_frame_pointer = 0;
    // 归一化后的符号名
    std::string normalized_symbol;
    // 归一化后的模块路径
    std::string normalized_module_path;
    // 归一化后的模块基址
    std::uintptr_t normalized_module_base = 0;
    // 归一化后的模块偏移
    std::uintptr_t normalized_module_offset = 0;
    // 归一化状态
    StackStatus status = StackStatus::Unchanged;
    // 调整原因
    std::string reason;
};

// 栈快照
struct StackTraceSnapshot {
    // 快照 ID
    std::uint64_t id = 0;
    // 采集时的单调时钟纳秒
    std::uint64_t monotonic_time_ns = 0;
    // 原始帧列表
    std::vector<RawStackFrame> raw_frames;
    // 归一化帧列表
    std::vector<NormalizedStackFrame> normalized_frames;
};

} // namespace stack
} // namespace runtime
} // namespace nyx
