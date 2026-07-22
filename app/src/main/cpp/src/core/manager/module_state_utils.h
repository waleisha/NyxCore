#pragma once

#include <string>

#include "src/core/manager/module_record.h"
#include "src/core/manager/module_snapshot.h"
#include "src/core/manager/module_spec.h"

namespace nyx {
namespace core {
namespace detail {

// 状态辅助函数

// 判断模块是否允许进入后续任务
inline bool runnable(const ModuleRecord& r) {
    return r.enabled && r.state != ModuleState::Failed;
}

// 判断模块是否已有正在执行的任务
inline bool busy(const ModuleRecord& r) {
    return r.auth_checking ||
        r.creating ||
        r.pre_initing ||
        r.initializing ||
        r.updating ||
        r.rendering;
}

// 判断任务里的实例指针是否仍指向当前实例
inline bool same_instance(const ModuleRecord& r, const sdk::IMod* mod) {
    return mod == nullptr || r.instance.get() == mod;
}

// 判断重复注册是否和已有记录一致
inline bool same_registration(const ModuleRecord& r, const ModuleSpec& s) {
    return r.feature == s.feature &&
        r.factory == s.factory &&
        r.enabled_by_default == s.enabled_by_default;
}

// 清空所有正在执行的任务标记
inline void clear_work_flags(ModuleRecord& r) {
    r.auth_checking = false;
    r.creating = false;
    r.pre_initing = false;
    r.initializing = false;
    r.updating = false;
    r.rendering = false;
}

// 根据进度字段推导对外生命周期状态
inline ModuleState state_from_progress(const ModuleRecord& r) {
    if (r.state == ModuleState::Failed) {
        return ModuleState::Failed;
    }
    if (!r.enabled) {
        return ModuleState::Disabled;
    }
    if (r.auth_checked && !r.authorized) {
        return ModuleState::BlockedByAuth;
    }
    if (r.initialized) {
        return ModuleState::Active;
    }
    if (r.pre_inited) {
        return ModuleState::PreInited;
    }
    if (r.instance) {
        return ModuleState::Created;
    }
    return ModuleState::Registered;
}

// 从内部记录生成对外快照
inline ModuleSnapshot make_snapshot(const ModuleRecord& r) {
    ModuleSnapshot s;
    s.name = r.name;
    s.feature = r.feature;
    s.state = r.state;
    s.found = true;
    s.enabled = r.enabled;
    s.authorized = r.authorized;
    s.update_count = r.update_count;
    s.render_count = r.render_count;
    s.last_error = r.last_error;
    return s;
}

// 错误信息格式化

// 格式化带异常详情的回调错误
inline std::string callback_error(const char* stage, const std::exception& e) {
    std::string t(stage);
    t += " failed: ";
    t += e.what();
    return t;
}

// 格式化不带异常详情的回调错误
inline std::string callback_error(const char* stage) {
    std::string t(stage);
    t += " failed";
    return t;
}

} // namespace detail
} // namespace core
} // namespace nyx
