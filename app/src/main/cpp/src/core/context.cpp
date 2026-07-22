#include "src/core/context.h"

namespace nyx {
    namespace core {

// 构造函数：记录启动时间、主线程ID，画面帧数从0开始计数
        Context::Context()
                : started_at_(std::chrono::steady_clock::now()),
                  main_thread_(std::this_thread::get_id()),
                  frame_(0) {}

// 全局唯一实例
        Context& Context::instance() {
            static Context context;
            return context;
        }

// 程序已运行时间（微秒）
        std::uint64_t Context::uptime_micros() const {
            const auto elapsed = std::chrono::steady_clock::now() - started_at_;
            return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
            );
        }

// 帧号加1，返回新的帧号
        std::uint64_t Context::next_frame() {
            return frame_.fetch_add(1, std::memory_order_relaxed) + 1;
        }

// 获取当前帧号
        std::uint64_t Context::frame() const {
            return frame_.load(std::memory_order_relaxed);
        }

// 判断当前线程是否为主线程
        bool Context::is_main_thread() const {
            std::lock_guard<std::mutex> lock(main_thread_mutex_);
            return std::this_thread::get_id() == main_thread_;
        }

// 将当前线程设为主线程
        void Context::bind_main_thread() {
            std::lock_guard<std::mutex> lock(main_thread_mutex_);
            main_thread_ = std::this_thread::get_id();
        }

// 判断当前线程是否为渲染线程
        bool Context::is_render_thread() const {
            std::lock_guard<std::mutex> lock(render_mutex_);
            return render_threads_.find(std::this_thread::get_id()) != render_threads_.end();
        }

// 设置/取消当前线程的渲染线程标记
        void Context::set_render_thread(bool enabled) {
            std::lock_guard<std::mutex> lock(render_mutex_);
            const auto id = std::this_thread::get_id();
            if (enabled) {
                render_threads_.insert(id);
            } else {
                render_threads_.erase(id);
            }
        }

    } // namespace core
} // namespace nyx