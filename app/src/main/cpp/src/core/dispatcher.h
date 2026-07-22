#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>

namespace nyx {
namespace core {

// 任务调度器：单例，缓存待执行任务并在排空时批量运行
class Dispatcher {
public:
    // 获取唯一实例
    static Dispatcher& instance();

    // 投递一个待执行任务，空任务会被忽略
    void post(std::function<void()> task);
    // 执行当前队列中的所有任务，返回执行数量
    std::size_t drain();
    // 获取等待执行的任务数量
    std::size_t pending() const;

private:
    // 私有构造，禁止外部创建
    Dispatcher() = default;

    // 保护任务队列的锁
    mutable std::mutex mutex_;
    // 待执行任务队列
    std::queue<std::function<void()>> tasks_;
};

} // namespace core
} // namespace nyx
