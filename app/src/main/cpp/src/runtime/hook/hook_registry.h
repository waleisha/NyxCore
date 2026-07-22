#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "src/runtime/hook/hook_backend.h"

namespace nyx::runtime::hook {

// 线程安全的 hook 注册表，按 owner 和 target 管理记录
class HookRegistry {
public:
    // 添加或替换未安装记录，拒绝和已安装 hook 冲突的请求
    bool add(HookRecord record);
    // 通过指定后端安装已登记记录
    RuntimeResult install(const std::string& owner, const std::string& target, HookBackend& backend);
    // 通过指定后端移除已安装记录
    RuntimeResult remove(const std::string& owner, const std::string& target, HookBackend& backend);
    // 获取单条记录快照
    std::optional<HookRecord> find(const std::string& owner, const std::string& target) const;
    // 获取全部记录快照
    std::vector<HookRecord> records() const;
    // 尽力移除已安装 hook，然后清空注册表
    void clear();

private:
    // 查找记录，调用方必须持有 mutex_
    HookRecord* find_locked(const std::string& owner, const std::string& target);
    // 查找记录，调用方必须持有 mutex_
    const HookRecord* find_locked(const std::string& owner, const std::string& target) const;

    // 保护 records_ 的锁
    mutable std::mutex mutex_;
    // hook 记录列表，按预期规模较小保留 vector 实现
    std::vector<HookRecord> records_;
};

} // namespace nyx::runtime::hook
