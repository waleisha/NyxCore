#include "src/core/dispatcher.h"

#include <exception>
#include <utility>

#include "sdk/include/utils.h"
#include "src/core/context.h"

namespace nyx {
namespace core {

// 全局唯一实例
Dispatcher& Dispatcher::instance() {
    static Dispatcher dispatcher;
    return dispatcher;
}

// 投递一个待执行任务，空任务会被忽略
void Dispatcher::post(std::function<void()> task) {
    if (!task) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push(std::move(task));
}

// 执行当前队列中的所有任务，返回执行数量
std::size_t Dispatcher::drain() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(tasks_);
    }

    std::size_t count = 0;
    while (!tasks.empty()) {
        try {
            tasks.front()();
        } catch (const std::exception& error) {
            NYX_LOGE("dispatcher task failed: %s", error.what());
        } catch (...) {
            NYX_LOGE("dispatcher task failed");
        }
        tasks.pop();
        ++count;
    }

    if (count > 0) {
        Context::instance().next_frame();
    }

    return count;
}

// 获取等待执行的任务数量
std::size_t Dispatcher::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

} // namespace core
} // namespace nyx

namespace nyx {
namespace sdk {
namespace utils {

// 投递一个待执行任务到核心调度器
void Post(std::function<void()> task) {
    core::Dispatcher::instance().post(std::move(task));
}

// 运行并清空核心调度器中的任务
std::size_t RunTasks() {
    return core::Dispatcher::instance().drain();
}

// 获取核心调度器中等待执行的任务数量
std::size_t TaskCount() {
    return core::Dispatcher::instance().pending();
}

// 判断当前线程是否为主线程
bool IsMain() {
    return core::Context::instance().is_main_thread();
}

// 程序已运行时间（微秒）
std::uint64_t Uptime() {
    return core::Context::instance().uptime_micros();
}

} // namespace utils
} // namespace sdk
} // namespace nyx
