#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace stack {

// CPU 栈上下文
struct StackCpuContext {
    // 程序计数器
    std::uintptr_t pc = 0;
    // 栈指针
    std::uintptr_t sp = 0;
    // 帧指针
    std::uintptr_t fp = 0;
    // 链接寄存器
    std::uintptr_t lr = 0;
    // 上下文是否有效
    bool valid = false;
    // 是否来自编译器内建接口
    bool from_builtin = false;
    // 是否来自 signal ucontext
    bool from_signal_context = false;
};

// 帧指针链节点
struct FramePointerNode {
    // 当前帧指针
    std::uintptr_t fp = 0;
    // 父帧指针
    std::uintptr_t parent_fp = 0;
    // 保存的返回地址
    std::uintptr_t return_address = 0;
    // 节点是否可信
    bool trusted = false;
    // 信任或失败原因
    std::string reason;
};

// 栈上下文读取器
class StackContextReader {
public:
    // 读取当前线程上下文
    RuntimeResult current(StackCpuContext* out) const;
    // 从 signal ucontext 读取上下文
    RuntimeResult from_signal_context(void* ucontext, StackCpuContext* out) const;
};

// 帧指针链遍历器
class FramePointerWalker {
public:
    // 按帧指针链采集节点
    RuntimeResult walk(
        const StackCpuContext& context,
        std::size_t max_frames,
        std::vector<FramePointerNode>* out
    ) const;
};

} // namespace stack
} // namespace runtime
} // namespace nyx
