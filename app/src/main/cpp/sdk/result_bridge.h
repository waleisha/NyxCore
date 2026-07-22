#pragma once

#include "sdk/include/utils.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace sdk {
namespace bridge {

// SDK 边界上的 runtime 状态转换。
inline Status status_from(runtime::RuntimeStatus status) {
    switch (status) {
        case runtime::RuntimeStatus::Ok:
            return Status::Ok;
        case runtime::RuntimeStatus::Disabled:
            return Status::Disabled;
        case runtime::RuntimeStatus::Unavailable:
            return Status::Unavailable;
        case runtime::RuntimeStatus::NotFound:
            return Status::NotFound;
        case runtime::RuntimeStatus::InvalidArgument:
            return Status::InvalidArgument;
        case runtime::RuntimeStatus::Denied:
            return Status::Denied;
        case runtime::RuntimeStatus::Failed:
            return Status::Failed;
    }

    NYX_LOGW("unknown runtime status: %d", static_cast<int>(status));
    return Status::Failed;
}

inline runtime::RuntimeStatus runtime_status_from(Status status) {
    switch (status) {
        case Status::Ok:
            return runtime::RuntimeStatus::Ok;
        case Status::Disabled:
            return runtime::RuntimeStatus::Disabled;
        case Status::Unavailable:
            return runtime::RuntimeStatus::Unavailable;
        case Status::NotFound:
            return runtime::RuntimeStatus::NotFound;
        case Status::InvalidArgument:
            return runtime::RuntimeStatus::InvalidArgument;
        case Status::Denied:
            return runtime::RuntimeStatus::Denied;
        case Status::Failed:
            return runtime::RuntimeStatus::Failed;
    }

    NYX_LOGW("unknown SDK status: %d", static_cast<int>(status));
    return runtime::RuntimeStatus::Failed;
}

inline Result result_from(const runtime::RuntimeResult& result) {
    return Result{status_from(result.status), result.detail};
}

} // namespace bridge
} // namespace sdk
} // namespace nyx
