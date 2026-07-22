#pragma once

#include "src/runtime/hook/hook_types.h"

namespace nyx::runtime::hook {

// Hook 后端接口，调用由 HookRegistry 串行化
class HookBackend {
public:
    virtual ~HookBackend() = default;

    // 安装 hook
    virtual RuntimeResult install(HookRecord& record) = 0;
    // 移除 hook
    virtual RuntimeResult remove(HookRecord& record) = 0;
};

} // namespace nyx::runtime::hook
