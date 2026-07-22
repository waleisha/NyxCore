#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <thread>

namespace nyx {
    namespace core {

// 全局上下文：单例，管理启动时间、帧号、线程身份
        class Context {
        public:
            // 获取唯一实例
            static Context& instance();

            // 程序已运行时间（微秒）
            std::uint64_t uptime_micros() const;
            // 帧号加1，返回新帧号
            std::uint64_t next_frame();
            // 获取当前帧号
            std::uint64_t frame() const;

            // 判断当前线程是否为主线程
            bool is_main_thread() const;
            // 将当前线程设为主线程
            void bind_main_thread();

            // 判断当前线程是否为渲染线程
            bool is_render_thread() const;
            // 设置/取消当前线程的渲染线程标记
            void set_render_thread(bool enabled);

        private:
            // 私有构造，禁止外部创建
            Context();

            // 启动时间点
            std::chrono::steady_clock::time_point started_at_;
            // 保护主线程ID的锁
            mutable std::mutex main_thread_mutex_;
            // 主线程ID
            std::thread::id main_thread_;
            // 当前帧号（原子，线程安全）
            std::atomic<std::uint64_t> frame_;
            // 保护渲染线程集合的锁
            mutable std::mutex render_mutex_;
            // 渲染线程ID集合
            std::unordered_set<std::thread::id> render_threads_;
        };

    } // namespace core
} // namespace nyx