#pragma once

#include <cstdint>
#include <string>

namespace nyx {
namespace core {

// 模块生命周期状态
enum class ModuleState {
    // 指定模块不存在
    NotFound,
    // 已注册，尚未创建实例
    Registered,
    // 实例已创建
    Created,
    // 已完成预初始化
    PreInited,
    // 已激活，可参与更新和渲染
    Active,
    // 已被手动禁用
    Disabled,
    // 鉴权未通过
    BlockedByAuth,
    // 回调或工厂执行失败
    Failed,
};

// 模块状态快照：对外只暴露可查询状态，不暴露内部工作标记
struct ModuleSnapshot {
    // 模块名称
    std::string name;
    // 鉴权功能标识
    std::string feature;
    // 当前生命周期状态
    ModuleState state = ModuleState::NotFound;
    // 是否找到该模块
    bool found = false;
    // 是否启用
    bool enabled = false;
    // 是否已通过鉴权
    bool authorized = false;
    // 已执行更新次数
    std::uint64_t update_count = 0;
    // 已执行渲染次数
    std::uint64_t render_count = 0;
    // 最近一次错误信息
    std::string last_error;
};

} // namespace core
} // namespace nyx
