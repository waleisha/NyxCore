#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace stack {

// 栈帧来源类型
enum class Type {
    Unknown,
    Application,
    Runtime,
    HookReplacement,
    HookTrampoline,
    PltHub,
    VendorHook,
    SignalHandler,
};

// 栈帧归一化状态
enum class Status {
    Unchanged,
    AdjustedReturnAddress,
    CollapsedHookFrame,
    RecoveredFramePointer,
    UntrustedFramePointer,
    UnmappedAddress,
    Failed,
};

// 一条栈帧快照
struct Frame {
    // 原始 PC
    std::uintptr_t raw_pc = 0;
    // 原始返回地址
    std::uintptr_t raw_return_address = 0;
    // 原始 frame pointer
    std::uintptr_t raw_frame_pointer = 0;
    // 归一化后的 PC
    std::uintptr_t normalized_pc = 0;
    // 归一化后的返回地址
    std::uintptr_t normalized_return_address = 0;
    // 归一化后的 frame pointer
    std::uintptr_t normalized_frame_pointer = 0;
    // 符号名
    std::string symbol;
    // 所属模块路径
    std::string module_path;
    // 栈帧类型
    Type kind = Type::Unknown;
    // 归一化状态
    Status status = Status::Unchanged;
    // 状态说明
    std::string reason;
    // 是否来自 unwind
    bool from_unwind = false;
    // 是否来自 frame chain
    bool from_frame_chain = false;
};

// 捕获当前线程栈帧
NYX_EXPORT Result Capture(std::vector<Frame>* out, std::size_t max_frames = 64);
// 捕获当前线程栈帧，失败信息保留在 Value 中
NYX_EXPORT Value<std::vector<Frame>> TryCapture(std::size_t max_frames = 64);
// 捕获当前线程栈帧，失败时返回空列表
NYX_EXPORT std::vector<Frame> Capture(std::size_t max_frames = 64);

} // namespace stack
} // namespace sdk
} // namespace nyx
