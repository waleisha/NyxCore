#include "sdk/include/stack.h"

#include "src/runtime/stack/stack_normalizer.h"
#include "sdk/result_bridge.h"

#include <utility>

namespace nyx {
namespace sdk {
namespace stack {

namespace {

// runtime 栈帧类型转 SDK 类型
Type to_kind(runtime::stack::StackType kind) {
    switch (kind) {
        case runtime::stack::StackType::Unknown:
            return Type::Unknown;
        case runtime::stack::StackType::Application:
            return Type::Application;
        case runtime::stack::StackType::Runtime:
            return Type::Runtime;
        case runtime::stack::StackType::HookReplacement:
            return Type::HookReplacement;
        case runtime::stack::StackType::HookTrampoline:
            return Type::HookTrampoline;
        case runtime::stack::StackType::PltHub:
            return Type::PltHub;
        case runtime::stack::StackType::VendorHook:
            return Type::VendorHook;
        case runtime::stack::StackType::SignalHandler:
            return Type::SignalHandler;
    }

    return Type::Unknown;
}

// runtime 栈帧状态转 SDK 状态
Status stack_to_sdk_status(runtime::stack::StackStatus status) {
    switch (status) {
        case runtime::stack::StackStatus::Unchanged:
            return Status::Unchanged;
        case runtime::stack::StackStatus::AdjustedReturnAddress:
            return Status::AdjustedReturnAddress;
        case runtime::stack::StackStatus::CollapsedHookFrame:
            return Status::CollapsedHookFrame;
        case runtime::stack::StackStatus::RecoveredFramePointer:
            return Status::RecoveredFramePointer;
        case runtime::stack::StackStatus::UntrustedFramePointer:
            return Status::UntrustedFramePointer;
        case runtime::stack::StackStatus::UnmappedAddress:
            return Status::UnmappedAddress;
        case runtime::stack::StackStatus::Failed:
            return Status::Failed;
    }

    return Status::Failed;
}

// runtime 归一化帧转 SDK 帧视图
Frame to_frame_view(const runtime::stack::NormalizedStackFrame& frame) {
    return Frame{
        frame.raw.pc,
        frame.raw.return_address,
        frame.raw.frame_pointer,
        frame.normalized_pc,
        frame.normalized_return_address,
        frame.normalized_frame_pointer,
        frame.normalized_symbol,
        frame.normalized_module_path,
        to_kind(frame.kind),
        stack_to_sdk_status(frame.status),
        frame.reason,
        frame.raw.from_unwind,
        frame.raw.from_frame_chain,
    };
}

} // namespace

// 采集归一化栈帧
Result Capture(std::vector<Frame>* out, std::size_t max_frames) {
    if (out == nullptr || max_frames == 0) {
        return Result{::nyx::sdk::Status::InvalidArgument, "missing stack output or frame limit"};
    }

    runtime::stack::StackNormalizer normalizer;
    runtime::stack::StackTraceSnapshot snapshot;
    runtime::stack::StackNormalizeRequest request;
    request.max_frames = max_frames;
    request.include_raw = false;
    const auto result = normalizer.capture(request, &snapshot);
    if (!result.ok()) {
        out->clear();
        return bridge::result_from(result);
    }

    out->clear();
    out->reserve(snapshot.normalized_frames.size());
    for (const auto& frame : snapshot.normalized_frames) {
        out->push_back(to_frame_view(frame));
    }
    return bridge::result_from(result);
}

// 采集栈帧并包装为 Value
Value<std::vector<Frame>> TryCapture(std::size_t max_frames) {
    Value<std::vector<Frame>> out;
    out.result = Capture(&out.value, max_frames);
    return out;
}

// 采集栈帧的简化接口
std::vector<Frame> Capture(std::size_t max_frames) {
    auto value = TryCapture(max_frames);
    if (!value.ok()) {
        NYX_LOGW("stack Capture failed: %s", value.result.detail.c_str());
        return {};
    }
    return std::move(value.value);
}

} // namespace stack
} // namespace sdk
} // namespace nyx
