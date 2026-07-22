#include "src/runtime/memory/memory_freeze.h"

#include "src/runtime/memory/memory_writer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 单个 owner 的冻结状态
struct FreezeState {
    // 保护状态的锁
    std::mutex mutex;
    // 唤醒冻结线程
    std::condition_variable cv;
    // 线程是否已启动
    bool running = false;
    // 是否请求停止
    bool stopping = false;
    // 目标进程
    MemProcess process;
    // 冻结项列表
    std::vector<FreezeItem> entries;
    // 写入间隔
    std::uint32_t delay_ms = 200;
    // 后台线程
    std::thread worker;
};

// 保护 owner 状态表的锁
std::mutex g_mutex;
// 按 owner 保存冻结状态
std::unordered_map<void*, std::shared_ptr<FreezeState>> g_states;

// 获取或创建 owner 状态
std::shared_ptr<FreezeState> state_for(void* owner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_states.find(owner);
    if (found != g_states.end()) {
        return found->second;
    }

    auto state = std::make_shared<FreezeState>();
    g_states.emplace(owner, state);
    return state;
}

// 查找 owner 状态
std::shared_ptr<FreezeState> find_state(void* owner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_states.find(owner);
    return found == g_states.end() ? nullptr : found->second;
}

// 删除 owner 状态
void erase_state(void* owner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_states.erase(owner);
}

// 后台冻结循环，按间隔反复写入目标值
void freeze_loop(const std::shared_ptr<FreezeState>& state) {
    for (;;) {
        std::vector<FreezeItem> entries;
        MemProcess process;
        std::uint32_t delay = 200;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            if (state->stopping) {
                break;
            }
            entries = state->entries;
            process = state->process;
            delay = state->delay_ms == 0 ? 1 : state->delay_ms;
        }

        if (!entries.empty()) {
            MemoryWriter writer(process);
            for (const auto& entry : entries) {
                writer.write(
                    entry.addr,
                    entry.scalar.bytes.data(),
                    entry.scalar.size,
                    WriteMode::AutoProtect
                );
            }
        }

        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock, std::chrono::milliseconds(delay), [&state] {
            return state->stopping;
        });
        if (state->stopping) {
            break;
        }
    }
}

// 校验 owner
RuntimeResult validate_owner(void* owner) {
    if (owner == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing freeze owner"};
    }
    return RuntimeResult{};
}

} // namespace

// 添加冻结项并立即写入一次
RuntimeResult add_freeze(void* owner, const FreezeItem& item, const MemProcess& process, std::uint32_t delay_ms) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }
    if (item.addr == 0 || item.scalar.size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing freeze address or value"};
    }

    MemoryWriter writer(process);
    auto wrote = writer.write(
        item.addr,
        item.scalar.bytes.data(),
        item.scalar.size,
        WriteMode::AutoProtect
    );
    if (!wrote.ok()) {
        return wrote;
    }

    auto state = state_for(owner);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->process = process;
        state->delay_ms = delay_ms == 0 ? 1 : delay_ms;
        state->entries.erase(
            std::remove_if(
                state->entries.begin(),
                state->entries.end(),
                [item](const FreezeItem& current) { return current.addr == item.addr; }
            ),
            state->entries.end()
        );
        state->entries.push_back(item);
    }
    state->cv.notify_all();
    return RuntimeResult{};
}

// 移除指定冻结项
RuntimeResult remove_freeze(void* owner, std::uintptr_t addr) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }
    if (addr == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing freeze address"};
    }

    auto state = find_state(owner);
    if (state == nullptr) {
        return RuntimeResult{RuntimeStatus::NotFound, "freeze state was not found"};
    }

    std::size_t old_size = 0;
    std::size_t new_size = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        old_size = state->entries.size();
        state->entries.erase(
            std::remove_if(
                state->entries.begin(),
                state->entries.end(),
                [addr](const FreezeItem& item) { return item.addr == addr; }
            ),
            state->entries.end()
        );
        new_size = state->entries.size();
    }
    state->cv.notify_all();

    if (new_size == old_size) {
        return RuntimeResult{RuntimeStatus::NotFound, "freeze item was not found"};
    }
    return RuntimeResult{};
}

// 清除 owner 的全部冻结项
RuntimeResult clear_freeze(void* owner) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }

    auto state = find_state(owner);
    if (state == nullptr) {
        return RuntimeResult{};
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->entries.clear();
    }
    state->cv.notify_all();
    return RuntimeResult{};
}

// 设置冻结循环间隔
RuntimeResult set_freeze_delay(void* owner, std::uint32_t delay_ms) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }
    if (delay_ms == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "freeze delay must be greater than zero"};
    }

    auto state = find_state(owner);
    if (state == nullptr) {
        return RuntimeResult{};
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->delay_ms = delay_ms;
    }
    state->cv.notify_all();
    return RuntimeResult{};
}

// 启动冻结线程
RuntimeResult start_freeze(void* owner, const MemProcess& process, std::uint32_t delay_ms) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }

    auto state = state_for(owner);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->process = process;
        state->delay_ms = delay_ms == 0 ? 1 : delay_ms;
        if (state->running) {
            return RuntimeResult{};
        }
        state->stopping = false;
        state->running = true;
    }

    state->worker = std::thread([state] {
        freeze_loop(state);
    });
    return RuntimeResult{};
}

// 停止冻结线程
RuntimeResult stop_freeze(void* owner) {
    auto valid = validate_owner(owner);
    if (!valid.ok()) {
        return valid;
    }

    auto state = find_state(owner);
    if (state == nullptr) {
        return RuntimeResult{};
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->stopping = true;
        state->running = false;
    }
    state->cv.notify_all();
    if (state->worker.joinable()) {
        state->worker.join();
    }
    erase_state(owner);
    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
