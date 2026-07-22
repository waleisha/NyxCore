#include "sdk/include/utils.h"

#include "src/utils/string/format.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <android/log.h>

#if NYX_ENABLE_NATIVE_LOGS
namespace {

constexpr std::size_t kQueueSize = 256;
constexpr std::size_t kMessageSize = 512;

int android_priority(nyx::sdk::utils::LogLevel level) {
    switch (level) {
        case nyx::sdk::utils::LogLevel::Debug:
            return ANDROID_LOG_DEBUG;
        case nyx::sdk::utils::LogLevel::Info:
            return ANDROID_LOG_INFO;
        case nyx::sdk::utils::LogLevel::Warning:
            return ANDROID_LOG_WARN;
        case nyx::sdk::utils::LogLevel::Error:
            return ANDROID_LOG_ERROR;
    }

    return ANDROID_LOG_INFO;
}

struct LogEntry {
    nyx::sdk::utils::LogLevel level = nyx::sdk::utils::LogLevel::Info;
    char text[kMessageSize] = {};
};

struct LogSlot {
    std::atomic<std::size_t> seq{0};
    LogEntry entry;
};

void write_entry(const LogEntry& entry) {
    __android_log_print(android_priority(entry.level), "NyxCore", "%s", entry.text);
}

void copy_text(char* out, std::size_t out_len, const char* text) {
    if (out == nullptr || out_len == 0) {
        return;
    }

    if (text == nullptr) {
        out[0] = '\0';
        return;
    }

    std::snprintf(out, out_len, "%s", text);
    out[out_len - 1] = '\0';
}

class LogQueue {
public:
    LogQueue() {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            slots_[i].seq.store(i, std::memory_order_relaxed);
        }
        worker_ = std::thread([this] {
            run();
        });
    }

    ~LogQueue() {
        stop();
    }

    void push(nyx::sdk::utils::LogLevel level, const char* text) {
        if (stopping_.load(std::memory_order_acquire)) {
            LogEntry entry;
            entry.level = level;
            copy_text(entry.text, sizeof(entry.text), text);
            write_entry(entry);
            return;
        }

        if (!enqueue(level, text)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        wake();
    }

    void flush() {
        wake();
        std::unique_lock<std::mutex> lock(drain_mutex_);
        drain_event_.wait_for(lock, std::chrono::seconds(2), [this] {
            return empty() && dropped_.load(std::memory_order_acquire) == 0;
        });
    }

    void stop() {
        std::lock_guard<std::mutex> lock(stop_mutex_);
        stopping_.store(true, std::memory_order_release);
        wake();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    bool enqueue(nyx::sdk::utils::LogLevel level, const char* text) {
        LogSlot* slot = nullptr;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[pos % kQueueSize];
            const std::size_t seq = slot->seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    )) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        slot->entry.level = level;
        copy_text(slot->entry.text, sizeof(slot->entry.text), text);
        slot->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool dequeue(LogEntry& out) {
        LogSlot* slot = nullptr;
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            slot = &slots_[pos % kQueueSize];
            const std::size_t seq = slot->seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed
                    )) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }

        out = slot->entry;
        slot->seq.store(pos + kQueueSize, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return dequeue_pos_.load(std::memory_order_acquire) ==
            enqueue_pos_.load(std::memory_order_acquire);
    }

    void flush_dropped() {
        const auto dropped = dropped_.exchange(0, std::memory_order_relaxed);
        if (dropped == 0) {
            return;
        }

        LogEntry entry;
        entry.level = nyx::sdk::utils::LogLevel::Warning;
        std::snprintf(entry.text, sizeof(entry.text), "dropped async log messages: %llu",
            static_cast<unsigned long long>(dropped));
        write_entry(entry);
    }

    void wait() {
        std::unique_lock<std::mutex> lock(wait_mutex_);
        wake_event_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return stopping_.load(std::memory_order_acquire) ||
                !empty() ||
                dropped_.load(std::memory_order_relaxed) != 0;
        });
    }

    void wake() {
        wake_event_.notify_one();
    }

    void run() {
        for (;;) {
            LogEntry entry;
            if (dequeue(entry)) {
                write_entry(entry);
                continue;
            }

            flush_dropped();
            if (empty() && dropped_.load(std::memory_order_acquire) == 0) {
                drain_event_.notify_all();
            }
            if (stopping_.load(std::memory_order_acquire) && empty()) {
                flush_dropped();
                drain_event_.notify_all();
                return;
            }

            wait();
        }
    }

    std::array<LogSlot, kQueueSize> slots_;
    std::atomic<std::size_t> enqueue_pos_{0};
    std::atomic<std::size_t> dequeue_pos_{0};
    std::atomic<unsigned long long> dropped_{0};
    std::atomic<bool> stopping_{false};
    std::mutex stop_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wake_event_;
    std::mutex drain_mutex_;
    std::condition_variable drain_event_;
    std::thread worker_;
};

LogQueue& queue() {
    static LogQueue instance;
    return instance;
}

} // namespace
#endif

namespace nyx {
namespace sdk {

const char* StatusStr(Status status) {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::Disabled:
            return "disabled";
        case Status::Unavailable:
            return "unavailable";
        case Status::NotFound:
            return "not_found";
        case Status::InvalidArgument:
            return "invalid_argument";
        case Status::Denied:
            return "denied";
        case Status::Failed:
            return "failed";
    }

    return "unknown";
}

namespace utils {

void Log(LogLevel level, const char* format, ...) {
#if NYX_ENABLE_NATIVE_LOGS
    if (format == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    const std::string text = ::nyx::utils::string::vformat(format, args);
    va_end(args);

    queue().push(level, text.c_str());
#else
    (void)level;
    (void)format;
#endif
}

void Flush() {
#if NYX_ENABLE_NATIVE_LOGS
    queue().flush();
#endif
}

void Close() {
#if NYX_ENABLE_NATIVE_LOGS
    queue().stop();
#endif
}

} // namespace utils
} // namespace sdk
} // namespace nyx
