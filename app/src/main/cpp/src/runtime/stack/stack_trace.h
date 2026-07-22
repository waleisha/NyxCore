#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace stack {

// unwinder 采集到的栈帧
struct StackFrame {
    // 程序计数器
    std::uintptr_t pc = 0;
    // 符号名
    std::string symbol;
    // 模块路径
    std::string module_path;
    // 模块基址
    std::uintptr_t module_base = 0;
    // 模块偏移
    std::uintptr_t module_offset = 0;
};

// 栈回溯采集器
class StackTrace {
public:
    // 采集当前线程栈帧
    RuntimeResult capture(std::vector<StackFrame>* out, std::size_t max_frames = 32) const;
};

} // namespace stack
} // namespace runtime
} // namespace nyx
