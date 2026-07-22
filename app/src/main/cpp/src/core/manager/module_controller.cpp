#include "src/core/manager/module_controller.h"

#include "sdk/include/auth.h"
#include "sdk/include/utils.h"

#include <exception>
#include <utility>

namespace nyx {
namespace core {

namespace {

// 默认鉴权入口：转发到 SDK auth 模块
bool default_can_start(const char* feature) {
    return sdk::auth::CanRun(feature);
}

// 格式化带异常详情的回调错误
std::string callback_error(const char* stage, const std::exception& error) {
    std::string text(stage);
    text += " failed: ";
    text += error.what();
    return text;
}

// 格式化不带异常详情的回调错误
std::string callback_error(const char* stage) {
    std::string text(stage);
    text += " failed";
    return text;
}

// 判断模块是否允许进入后续任务
bool runnable(const ModuleRecord& record) {
    return record.enabled && record.state != ModuleState::Failed;
}

// 判断模块是否已有正在执行的任务
bool busy(const ModuleRecord& record) {
    return record.auth_checking ||
        record.creating ||
        record.pre_initing ||
        record.initializing ||
        record.updating ||
        record.rendering;
}

// 判断任务里的实例指针是否仍指向当前实例
bool same_instance(const ModuleRecord& record, const sdk::IMod* mod) {
    return mod == nullptr || record.instance.get() == mod;
}

// 判断重复注册是否和已有记录一致
bool same_registration(const ModuleRecord& record, const ModuleSpec& spec) {
    return record.feature == spec.feature &&
        record.factory == spec.factory &&
        record.enabled_by_default == spec.enabled_by_default;
}

// 清空所有正在执行的任务标记
void clear_work_flags(ModuleRecord& record) {
    record.auth_checking = false;
    record.creating = false;
    record.pre_initing = false;
    record.initializing = false;
    record.updating = false;
    record.rendering = false;
}

// 根据进度字段推导对外生命周期状态
ModuleState state_from_progress(const ModuleRecord& record) {
    if (record.state == ModuleState::Failed) {
        return ModuleState::Failed;
    }
    if (!record.enabled) {
        return ModuleState::Disabled;
    }
    if (record.auth_checked && !record.authorized) {
        return ModuleState::BlockedByAuth;
    }
    if (record.initialized) {
        return ModuleState::Active;
    }
    if (record.pre_inited) {
        return ModuleState::PreInited;
    }
    if (record.instance) {
        return ModuleState::Created;
    }
    return ModuleState::Registered;
}

} // namespace

// 使用默认鉴权入口创建控制器
ModuleController::ModuleController() : ModuleController(AuthCheck{}) {}

// 使用自定义鉴权入口创建控制器
ModuleController::ModuleController(AuthCheck auth_check)
    : auth_check_(std::move(auth_check)) {}

// 获取全局唯一实例
ModuleController& ModuleController::Instance() {
    static ModuleController controller;
    return controller;
}

// 注册一个模块规格
bool ModuleController::Register(ModuleSpec spec) {
    if (spec.name.empty() || spec.factory == nullptr) {
        NYX_LOGE("module registration rejected an invalid entry");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& record : records_) {
        if (record.name == spec.name) {
            if (same_registration(record, spec)) {
                NYX_LOGD("module already registered: %s", spec.name.c_str());
                return true;
            }
            NYX_LOGE("module registration conflicts with existing entry: %s", spec.name.c_str());
            return false;
        }
    }

    ModuleRecord record;
    record.name = std::move(spec.name);
    record.feature = std::move(spec.feature);
    record.factory = spec.factory;
    record.enabled_by_default = spec.enabled_by_default;
    record.enabled = spec.enabled_by_default;
    record.authorized = record.feature.empty();
    record.auth_checked = record.feature.empty();
    record.state = state_from_progress(record);
    records_.push_back(std::move(record));
    NYX_LOGI("module registered: %s", records_.back().name.c_str());
    return true;
}

// 获取已注册模块数量
std::size_t ModuleController::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

// 执行鉴权、创建实例并调用模块初始化回调
void ModuleController::PreInitAll() {
    RunAuthTasks(AuthTasks());

    for (const auto& task : CreateTasks()) {
        std::unique_ptr<sdk::IMod> instance;
        std::string error;
        try {
            instance = task.factory != nullptr ? task.factory() : nullptr;
            if (!instance) {
                error = "factory returned null";
            }
        } catch (const std::exception& ex) {
            error = callback_error("factory", ex);
        } catch (...) {
            error = callback_error("factory");
        }
        FinishCreate(task, std::move(instance), std::move(error));
    }

    for (const auto& task : PreInitTasks()) {
        try {
            NYX_LOGI("module init: %s", task.name.c_str());
            task.mod->OnInit();
            FinishPreInit(task);
        } catch (const std::exception& ex) {
            Fail(task, callback_error("init", ex));
        } catch (...) {
            Fail(task, callback_error("init"));
        }
    }
}

// 执行初始化阶段需要的鉴权检查
void ModuleController::InitializeAll() {
    RunAuthTasks(AuthTasks());
}

// 更新所有已激活模块
void ModuleController::UpdateAll() {
    for (const auto& task : UpdateTasks()) {
        try {
            task.mod->OnUpdate();
            FinishUpdate(task);
        } catch (const std::exception& ex) {
            Fail(task, callback_error("update", ex));
        } catch (...) {
            Fail(task, callback_error("update"));
        }
    }
}

// 渲染所有已激活模块
void ModuleController::RenderAll() {
    for (const auto& task : RenderTasks()) {
        try {
            task.mod->OnDraw();
            FinishRender(task);
        } catch (const std::exception& ex) {
            Fail(task, callback_error("render", ex));
        } catch (...) {
            Fail(task, callback_error("render"));
        }
    }
}

// 设置指定模块启用状态
bool ModuleController::SetEnabled(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        if (record.name != name) {
            continue;
        }

        record.enabled = enabled;
        ++record.generation;
        record.state = state_from_progress(record);
        RefreshActiveCacheLocked();
        NYX_LOGI("module %s: %s", enabled ? "enabled" : "disabled", record.name.c_str());
        return true;
    }
    return false;
}

// 判断指定模块是否启用
bool ModuleController::IsEnabled(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& record : records_) {
        if (record.name == name) {
            return record.enabled;
        }
    }
    return false;
}

// 启用所有模块
void ModuleController::EnableAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        record.enabled = true;
        ++record.generation;
        record.state = state_from_progress(record);
    }
    RefreshActiveCacheLocked();
}

// 禁用所有模块
void ModuleController::DisableAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& record : records_) {
        record.enabled = false;
        ++record.generation;
        record.state = state_from_progress(record);
    }
    RefreshActiveCacheLocked();
}

// 重新检查所有需要鉴权的模块
void ModuleController::RefreshAuth() {
    RunAuthTasks(RefreshTasks());
}

// 获取全部模块状态快照
std::vector<ModuleSnapshot> ModuleController::Snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModuleSnapshot> snapshots;
    snapshots.reserve(records_.size());
    for (const auto& record : records_) {
        snapshots.push_back(MakeSnapshot(record));
    }
    return snapshots;
}

// 获取指定模块状态快照
ModuleSnapshot ModuleController::Snapshot(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& record : records_) {
        if (record.name == name) {
            return MakeSnapshot(record);
        }
    }

    ModuleSnapshot snapshot;
    snapshot.name = name;
    return snapshot;
}

// 收集可创建实例的模块任务
std::vector<ModuleController::CreateTask> ModuleController::CreateTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CreateTask> tasks;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        auto& record = records_[index];
        if (
            !runnable(record) ||
            busy(record) ||
            record.instance ||
            !record.auth_checked ||
            !record.authorized
        ) {
            continue;
        }

        record.creating = true;
        record.state = ModuleState::Registered;
        tasks.push_back(CreateTask{index, record.generation, record.name, record.factory});
    }
    return tasks;
}

// 收集首次鉴权任务
std::vector<ModuleController::AuthTask> ModuleController::AuthTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuthTask> tasks;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        auto& record = records_[index];
        if (!runnable(record) || busy(record) || record.auth_checked) {
            continue;
        }

        record.auth_checking = true;
        tasks.push_back(AuthTask{index, record.generation, record.name, record.feature});
    }
    return tasks;
}

// 收集重新鉴权任务
std::vector<ModuleController::AuthTask> ModuleController::RefreshTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuthTask> tasks;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        auto& record = records_[index];
        if (record.state == ModuleState::Failed || record.feature.empty() || busy(record)) {
            continue;
        }

        record.auth_checking = true;
        tasks.push_back(AuthTask{index, record.generation, record.name, record.feature});
    }
    return tasks;
}

// 收集可预初始化的模块任务
std::vector<ModuleController::ModuleTask> ModuleController::PreInitTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModuleTask> tasks;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        auto& record = records_[index];
        if (
            !runnable(record) ||
            busy(record) ||
            !record.instance ||
            record.pre_inited ||
            !record.auth_checked ||
            !record.authorized
        ) {
            continue;
        }

        record.pre_initing = true;
        tasks.push_back(ModuleTask{index, record.generation, record.name, record.instance.get()});
    }
    return tasks;
}

// 收集可初始化的模块任务
std::vector<ModuleController::ModuleTask> ModuleController::InitializeTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModuleTask> tasks;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        auto& record = records_[index];
        if (
            !runnable(record) ||
            busy(record) ||
            !record.instance ||
            !record.pre_inited ||
            record.initialized
        ) {
            continue;
        }
        if (!record.auth_checked || !record.authorized) {
            continue;
        }

        record.initializing = true;
        tasks.push_back(ModuleTask{index, record.generation, record.name, record.instance.get()});
    }
    return tasks;
}

// 收集可更新的模块任务
std::vector<ModuleController::ModuleTask> ModuleController::UpdateTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModuleTask> tasks;
    tasks.reserve(active_indexes_.size());
    for (const auto index : active_indexes_) {
        if (index >= records_.size()) {
            continue;
        }

        auto& record = records_[index];
        if (busy(record) || record.state != ModuleState::Active || !record.instance) {
            continue;
        }

        record.updating = true;
        tasks.push_back(ModuleTask{index, record.generation, record.name, record.instance.get()});
    }
    return tasks;
}

// 收集可渲染的模块任务
std::vector<ModuleController::ModuleTask> ModuleController::RenderTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModuleTask> tasks;
    tasks.reserve(active_indexes_.size());
    for (const auto index : active_indexes_) {
        if (index >= records_.size()) {
            continue;
        }

        auto& record = records_[index];
        if (busy(record) || record.state != ModuleState::Active || !record.instance) {
            continue;
        }

        record.rendering = true;
        tasks.push_back(ModuleTask{index, record.generation, record.name, record.instance.get()});
    }
    return tasks;
}

// 回写创建实例结果，过期任务不覆盖新状态
void ModuleController::FinishCreate(const CreateTask& task, std::unique_ptr<sdk::IMod> instance, std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.creating = false;
    if (record.generation != task.generation || record.instance) {
        RefreshActiveCacheLocked();
        return;
    }

    if (!error.empty() || !instance) {
        record.state = ModuleState::Failed;
        record.last_error = error.empty() ? "factory returned null" : std::move(error);
        ++record.generation;
        RefreshActiveCacheLocked();
        NYX_LOGE("module create failed: %s: %s", record.name.c_str(), record.last_error.c_str());
        return;
    }

    record.instance = std::move(instance);
    record.last_error.clear();
    ++record.generation;
    record.state = state_from_progress(record);
    RefreshActiveCacheLocked();
}

// 回写鉴权结果，过期任务不覆盖新状态
void ModuleController::FinishAuth(const AuthTask& task, bool authorized, std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.auth_checking = false;
    if (record.generation != task.generation || record.state == ModuleState::Failed) {
        RefreshActiveCacheLocked();
        return;
    }

    record.auth_checked = true;
    record.authorized = authorized;
    if (!authorized && !error.empty()) {
        record.last_error = std::move(error);
        NYX_LOGW("module auth check failed: %s: %s", record.name.c_str(), record.last_error.c_str());
    } else if (!authorized) {
        record.last_error = "authorization blocked";
        NYX_LOGW("module blocked by auth: %s", record.name.c_str());
    } else {
        record.last_error.clear();
    }
    ++record.generation;
    record.state = state_from_progress(record);
    RefreshActiveCacheLocked();
}

// 回写预初始化成功结果
void ModuleController::FinishPreInit(const ModuleTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.pre_initing = false;
    if (
        record.generation != task.generation ||
        !same_instance(record, task.mod) ||
        record.state == ModuleState::Failed
    ) {
        RefreshActiveCacheLocked();
        return;
    }

    record.pre_inited = true;
    record.initialized = true;
    record.last_error.clear();
    ++record.generation;
    record.state = state_from_progress(record);
    RefreshActiveCacheLocked();
}

// 回写初始化成功结果
void ModuleController::FinishInitialize(const ModuleTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.initializing = false;
    if (
        record.generation != task.generation ||
        !same_instance(record, task.mod) ||
        record.state == ModuleState::Failed
    ) {
        RefreshActiveCacheLocked();
        return;
    }

    record.initialized = true;
    record.last_error.clear();
    ++record.generation;
    record.state = state_from_progress(record);
    RefreshActiveCacheLocked();
}

// 回写更新成功结果
void ModuleController::FinishUpdate(const ModuleTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.updating = false;
    if (
        record.generation != task.generation ||
        !same_instance(record, task.mod) ||
        record.state != ModuleState::Active
    ) {
        RefreshActiveCacheLocked();
        return;
    }

    ++record.update_count;
}

// 回写渲染成功结果
void ModuleController::FinishRender(const ModuleTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    record.rendering = false;
    if (
        record.generation != task.generation ||
        !same_instance(record, task.mod) ||
        record.state != ModuleState::Active
    ) {
        RefreshActiveCacheLocked();
        return;
    }

    ++record.render_count;
}

// 标记模块失败并记录错误
void ModuleController::Fail(const ModuleTask& task, std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (task.index >= records_.size()) {
        return;
    }

    auto& record = records_[task.index];
    if (record.generation != task.generation || !same_instance(record, task.mod)) {
        clear_work_flags(record);
        RefreshActiveCacheLocked();
        return;
    }

    clear_work_flags(record);
    record.state = ModuleState::Failed;
    record.last_error = std::move(error);
    ++record.generation;
    RefreshActiveCacheLocked();
    NYX_LOGE("module failed: %s: %s", record.name.c_str(), record.last_error.c_str());
}

// 在锁外执行鉴权，再回写结果
void ModuleController::RunAuthTasks(std::vector<AuthTask> tasks) {
    for (const auto& task : tasks) {
        bool authorized = false;
        std::string error;
        try {
            authorized = CanStart(task.feature);
        } catch (const std::exception& ex) {
            error = callback_error("authorization", ex);
        } catch (...) {
            error = callback_error("authorization");
        }

        FinishAuth(task, authorized && error.empty(), std::move(error));
    }
}

// 判断功能是否允许启动，空功能默认允许
bool ModuleController::CanStart(const std::string& feature) const {
    if (feature.empty()) {
        return true;
    }

    if (auth_check_) {
        return auth_check_(feature.c_str());
    }
    return default_can_start(feature.c_str());
}

// 从内部记录生成对外快照
ModuleSnapshot ModuleController::MakeSnapshot(const ModuleRecord& record) const {
    ModuleSnapshot snapshot;
    snapshot.name = record.name;
    snapshot.feature = record.feature;
    snapshot.state = record.state;
    snapshot.found = true;
    snapshot.enabled = record.enabled;
    snapshot.authorized = record.authorized;
    snapshot.update_count = record.update_count;
    snapshot.render_count = record.render_count;
    snapshot.last_error = record.last_error;
    return snapshot;
}

// 刷新已激活模块下标缓存，调用方必须持有 mutex_
void ModuleController::RefreshActiveCacheLocked() {
    active_indexes_.clear();
    active_indexes_.reserve(records_.size());
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const auto& record = records_[index];
        if (record.state == ModuleState::Active && record.instance) {
            active_indexes_.push_back(index);
        }
    }
}

} // namespace core
} // namespace nyx
