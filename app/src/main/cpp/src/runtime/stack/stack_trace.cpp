#include "src/runtime/stack/stack_trace.h"

#include "src/runtime/stack/stack_base.h"

#include <unwind.h>

#include <vector>

namespace nyx {
namespace runtime {
namespace stack {

namespace {

// _Unwind_Backtrace 采集状态
struct UnwindState {
    // 输出 PC 列表
    std::vector<std::uintptr_t>* pcs = nullptr;
    // 最大帧数
    std::size_t max_frames = 0;
};

// unwinder 回调，逐帧收集 PC
_Unwind_Reason_Code collect_frame(_Unwind_Context* context, void* arg) {
    auto* state = static_cast<UnwindState*>(arg);
    if (state == nullptr || state->pcs == nullptr || state->pcs->size() >= state->max_frames) {
        return _URC_END_OF_STACK;
    }

    const auto pc = code_address(static_cast<std::uintptr_t>(_Unwind_GetIP(context)));
    if (pc != 0) {
        state->pcs->push_back(pc);
    }

    return state->pcs->size() >= state->max_frames ? _URC_END_OF_STACK : _URC_NO_REASON;
}

// 用 dladdr 信息补全栈帧
StackFrame frame_from(std::uintptr_t pc) {
    StackFrame frame;
    frame.pc = code_address(pc);

    const auto info = query_dladdr(frame.pc);
    frame.symbol = info.symbol;
    frame.module_path = info.module_path;
    frame.module_base = info.module_base;
    frame.module_offset = info.module_offset;

    return frame;
}

} // namespace

// 采集当前线程栈帧
RuntimeResult StackTrace::capture(std::vector<StackFrame>* out, std::size_t max_frames) const {
    if (out == nullptr || max_frames == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing output frames or frame limit"};
    }

    out->clear();
    std::vector<std::uintptr_t> pcs;
    pcs.reserve(max_frames);

    UnwindState state;
    state.pcs = &pcs;
    state.max_frames = max_frames;
    _Unwind_Backtrace(collect_frame, &state);

    for (const auto pc : pcs) {
        out->push_back(frame_from(pc));
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::Unavailable, "stack trace unwinder produced no frames"};
    }

    return RuntimeResult{};
}

} // namespace stack
} // namespace runtime
} // namespace nyx
