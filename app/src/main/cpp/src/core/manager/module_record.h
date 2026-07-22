#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "src/core/manager/module_spec.h"
#include "src/core/manager/module_snapshot.h"

namespace nyx {
namespace core {

// 模块内部记录：保存生命周期、鉴权和正在执行的任务状态
struct ModuleRecord {
    // 模块名称，作为唯一注册键
    std::string name;
    // 鉴权功能标识，空字符串表示无需鉴权
    std::string feature;
    // 模块实例工厂
    sdk::Factory factory = nullptr;
    // 模块实例
    std::unique_ptr<sdk::IMod> instance;
    // 注册时的默认启用状态
    bool enabled_by_default = true;
    // 当前是否启用
    bool enabled = true;
    // 当前鉴权是否通过
    bool authorized = false;
    // 是否已经完成鉴权检查
    bool auth_checked = false;
    // 是否正在鉴权
    bool auth_checking = false;
    // 是否正在创建实例
    bool creating = false;
    // 是否正在执行预初始化
    bool pre_initing = false;
    // 是否正在执行初始化
    bool initializing = false;
    // 是否正在执行更新
    bool updating = false;
    // 是否正在执行渲染
    bool rendering = false;
    // 是否完成预初始化
    bool pre_inited = false;
    // 是否完成初始化
    bool initialized = false;
    // 当前生命周期状态
    ModuleState state = ModuleState::Registered;
    // 状态代数，用于丢弃旧任务回写
    std::uint64_t generation = 0;
    // 已执行更新次数
    std::uint64_t update_count = 0;
    // 已执行渲染次数
    std::uint64_t render_count = 0;
    // 最近一次错误信息
    std::string last_error;
};

} // namespace core
} // namespace nyx
