#pragma once

#include <memory>
#include <string>

#include "src/runtime/runtime_result.h"

namespace nyx::runtime::hook {

// Hook 实现类型
enum class HookKind {
    // PLT/GOT hook
    Plt,
    // 指令级 inline hook
    Inline,
};

// Hook 生命周期状态，由 HookRegistry 维护
enum class HookState {
    // 已登记，尚未安装
    Registered,
    // 已安装
    Installed,
    // 已移除
    Removed,
    // 安装或移除失败
    Failed,
};

// 获取生命周期状态的稳定诊断名称
inline const char* state_name(HookState state) {
    switch (state) {
        case HookState::Registered: return "registered";
        case HookState::Installed: return "installed";
        case HookState::Removed: return "removed";
        case HookState::Failed: return "failed";
    }
    return "unknown";
}

// Runtime 持有的 hook 记录，target 是内部去重键，不是对外地址
struct HookRecord {
    // 记录归属方
    std::string owner;
    // 内部 registry key
    std::string target;
    // PLT caller 动态库路径，空值表示不限制
    std::string caller_path;
    // PLT callee 动态库路径，空值表示不限制
    std::string callee_path;
    // PLT 符号名
    std::string symbol;
    // Hook 实现类型
    HookKind kind = HookKind::Plt;
    // inline hook 目标地址
    void* target_address = nullptr;
    // 替换函数地址
    void* replacement = nullptr;
    // 原函数或 trampoline 地址
    void* original = nullptr;
    // 后端私有状态，保持到卸载完成
    std::shared_ptr<void> backend_data;
    // 当前生命周期状态
    HookState state = HookState::Registered;
    // 最近一次后端执行结果
    RuntimeResult result;
};

} // namespace nyx::runtime::hook
