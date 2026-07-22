#pragma once

#include <cstddef>

#include "src/runtime/hook/hook_backend.h"

namespace nyx::runtime::hook {

// 单条 PLT hook 的安装统计
struct PltHookStats {
    // 成功 hook 的位置数量
    std::size_t hooked_count = 0;
    // 失败的位置数量
    std::size_t failed_count = 0;
};

// 判断当前 ABI 是否编译并初始化了 PLT 后端
bool plt_hook_available();

// 获取 hook 记录里的 PLT 安装统计
PltHookStats plt_stats(const HookRecord& record);

// 在 replacement 内平衡 ByteHook automatic mode 的调用栈
void plt_pop_stack(void* return_address);

// 获取进程级 ByteHook PLT hook 后端
HookBackend& plt_backend();

} // namespace nyx::runtime::hook
