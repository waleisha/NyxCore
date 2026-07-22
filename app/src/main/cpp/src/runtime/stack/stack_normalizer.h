#pragma once

#include <cstddef>
#include <vector>

#include "src/runtime/stack/stack_frame.h"
#include "src/runtime/stack/stack_hooks.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace stack {

// 栈归一化请求
struct StackNormalizeRequest {
    // 最大帧数
    std::size_t max_frames = 64;
    // 是否保留原始帧
    bool include_raw = true;
    // 是否折叠 Hook 辅助帧
    bool collapse_hook_frames = true;
    // 是否恢复返回地址
    bool recover_return_address = true;
    // 是否恢复帧指针
    bool recover_frame_pointer = true;
    // 是否临时修复 live stack 辅助 unwinder
    bool repair_live_stack = true;
    // signal ucontext，上下文为空时采集当前线程
    void* signal_ucontext = nullptr;
};

// 栈归一化器
class StackNormalizer {
public:
    // 采集并归一化栈快照
    RuntimeResult capture(const StackNormalizeRequest& request, StackTraceSnapshot* out) const;
    // 使用默认请求归一化原始帧
    RuntimeResult normalize(
        const std::vector<RawStackFrame>& raw,
        const std::vector<HookFrameRecord>& hooks,
        std::vector<NormalizedStackFrame>* out
    ) const;
    // 按请求选项归一化原始帧
    RuntimeResult normalize(
        const std::vector<RawStackFrame>& raw,
        const std::vector<HookFrameRecord>& hooks,
        const StackNormalizeRequest& request,
        std::vector<NormalizedStackFrame>* out
    ) const;
};

} // namespace stack
} // namespace runtime
} // namespace nyx
