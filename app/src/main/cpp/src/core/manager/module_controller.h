#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "src/core/manager/module_record.h"
#include "src/core/manager/module_spec.h"
#include "src/core/manager/module_snapshot.h"

namespace nyx {
namespace core {

// 模块控制器：注册模块，驱动鉴权、创建、初始化、更新和渲染
class ModuleController {
public:
    // 鉴权检查回调，返回模块功能是否允许启动
    using AuthCheck = std::function<bool(const char*)>;

    // 使用默认鉴权入口创建控制器
    ModuleController();
    // 使用自定义鉴权入口创建控制器，便于测试或替换鉴权来源
    explicit ModuleController(AuthCheck auth_check);

    // 控制器持有模块实例，禁止复制
    ModuleController(const ModuleController&) = delete;
    ModuleController& operator=(const ModuleController&) = delete;

    // 获取全局唯一实例
    static ModuleController& Instance();

    // 注册一个模块规格
    bool Register(ModuleSpec spec);
    // 获取已注册模块数量
    std::size_t Count() const;

    // 执行鉴权、创建实例并调用模块初始化回调
    void PreInitAll();
    // 执行初始化阶段需要的鉴权检查
    void InitializeAll();
    // 更新所有已激活模块
    void UpdateAll();
    // 渲染所有已激活模块
    void RenderAll();

    // 设置指定模块启用状态
    bool SetEnabled(const std::string& name, bool enabled);
    // 判断指定模块是否启用
    bool IsEnabled(const std::string& name) const;
    // 启用所有模块
    void EnableAll();
    // 禁用所有模块
    void DisableAll();

    // 重新检查所有需要鉴权的模块
    void RefreshAuth();

    // 获取全部模块状态快照
    std::vector<ModuleSnapshot> Snapshots() const;
    // 获取指定模块状态快照
    ModuleSnapshot Snapshot(const std::string& name) const;

private:
    // 创建实例任务：保存记录位置和代数，避免旧任务回写新状态
    struct CreateTask {
        // records_ 下标
        std::size_t index = 0;
        // 任务创建时的状态代数
        std::uint64_t generation = 0;
        // 模块名称
        std::string name;
        // 模块实例工厂
        sdk::Factory factory = nullptr;
    };

    // 鉴权任务：在锁外检查权限，在锁内回写结果
    struct AuthTask {
        // records_ 下标
        std::size_t index = 0;
        // 任务创建时的状态代数
        std::uint64_t generation = 0;
        // 模块名称
        std::string name;
        // 鉴权功能标识
        std::string feature;
    };

    // 模块回调任务：保存实例指针，回写时确认实例未被替换
    struct ModuleTask {
        // records_ 下标
        std::size_t index = 0;
        // 任务创建时的状态代数
        std::uint64_t generation = 0;
        // 模块名称
        std::string name;
        // 模块实例指针
        sdk::IMod* mod = nullptr;
    };

    // 收集可创建实例的模块任务
    std::vector<CreateTask> CreateTasks();
    // 收集首次鉴权任务
    std::vector<AuthTask> AuthTasks();
    // 收集重新鉴权任务
    std::vector<AuthTask> RefreshTasks();
    // 收集可预初始化的模块任务
    std::vector<ModuleTask> PreInitTasks();
    // 收集可初始化的模块任务
    std::vector<ModuleTask> InitializeTasks();
    // 收集可更新的模块任务
    std::vector<ModuleTask> UpdateTasks();
    // 收集可渲染的模块任务
    std::vector<ModuleTask> RenderTasks();

    // 回写创建实例结果
    void FinishCreate(const CreateTask& task, std::unique_ptr<sdk::IMod> instance, std::string error);
    // 回写鉴权结果
    void FinishAuth(const AuthTask& task, bool authorized, std::string error = {});
    // 回写预初始化成功结果
    void FinishPreInit(const ModuleTask& task);
    // 回写初始化成功结果
    void FinishInitialize(const ModuleTask& task);
    // 回写更新成功结果
    void FinishUpdate(const ModuleTask& task);
    // 回写渲染成功结果
    void FinishRender(const ModuleTask& task);
    // 标记模块失败并记录错误
    void Fail(const ModuleTask& task, std::string error);
    // 执行鉴权任务并回写结果
    void RunAuthTasks(std::vector<AuthTask> tasks);

    // 判断功能是否允许启动
    bool CanStart(const std::string& feature) const;
    // 从内部记录生成对外快照
    ModuleSnapshot MakeSnapshot(const ModuleRecord& record) const;
    // 刷新已激活模块下标缓存，调用方必须持有 mutex_
    void RefreshActiveCacheLocked();

    // 保护模块记录和激活缓存的锁
    mutable std::mutex mutex_;
    // 模块记录列表
    std::vector<ModuleRecord> records_;
    // 已激活模块的 records_ 下标缓存
    std::vector<std::size_t> active_indexes_;
    // 鉴权检查回调
    AuthCheck auth_check_;
};

} // namespace core
} // namespace nyx
