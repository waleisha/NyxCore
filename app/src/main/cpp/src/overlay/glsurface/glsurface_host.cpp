#include "src/overlay/glsurface/glsurface_host.h"

#include <mutex>
#include <time.h>

#include <android/native_window_jni.h>

#include "sdk/include/utils.h"

namespace nyx {
namespace overlay {
namespace glsurface {

namespace {

// GLSurface Host 全局状态
struct HostState {
    // 当前 native window
    ANativeWindow* window = nullptr;
    // Surface 宽度
    int width = 0;
    // Surface 高度
    int height = 0;
    // 屏幕密度
    float density = 1.0f;

    // 最近绑定的 EGL Context
    EGLContext last_egl_context = EGL_NO_CONTEXT;
    // 当前渲染线程
    std::atomic<pthread_t> render_thread{0};
    // Surface generation
    std::atomic<uint64_t> generation{0};
    // 是否已初始化
    std::atomic<bool> initialized{false};
    // Surface 是否存活
    std::atomic<bool> surface_alive{false};
    // 是否已请求 View 重建
    std::atomic<bool> recreate_view_pending{false};
    // 上次请求重建时间
    std::atomic<int64_t> last_recreate_ms{0};
    // 已记录旧渲染线程日志的 generation
    std::atomic<uint64_t> stale_thread_logged_generation{0};

    // 连续失败次数
    int consecutive_failures = 0;
    // 首次失败时间
    int64_t first_failure_ms = 0;
};

// 保护 Host 状态的锁
std::mutex host_mutex;
// Host 状态
HostState host;

// 触发重建的连续失败阈值
constexpr int kRecreateAfterConsecutiveFailures = 8;
// 触发重建的失败持续时间
constexpr int64_t kRecreateAfterFailureDurationMs = 700;
// 重建请求防抖时间
constexpr int64_t kRecreateDebounceMs = 3000;

// 清理模式名称
const char* CleanupModeNameInternal(CleanupMode mode) {
    switch (mode) {
        case CleanupMode::NormalSurfaceDestroyed:
            return "NormalSurfaceDestroyed";
        case CleanupMode::ForcedSurfaceRecreate:
            return "ForcedSurfaceRecreate";
        case CleanupMode::RuntimeUnload:
            return "RuntimeUnload";
        default:
            return "Unknown";
    }
}

// 获取单调时钟毫秒
int64_t NowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// 安全释放或遗弃 native window
void SafeReleaseWindow(CleanupMode mode) {
    if (host.window == nullptr) {
        return;
    }

    if (mode == CleanupMode::ForcedSurfaceRecreate) {
        // 强制重建路径下旧 window 指针可能已悬空，不能触碰
        NYX_LOGW("glsurface abandon old window without touching: window=%p mode=%s",
                 host.window, CleanupModeNameInternal(mode));
        host.window = nullptr;
        return;
    }

    ANativeWindow_release(host.window);
    NYX_LOGD("glsurface window released: window=%p mode=%s",
             host.window, CleanupModeNameInternal(mode));
    host.window = nullptr;
}

// 清理 Surface 状态
void ClearState(CleanupMode mode) {
    host.surface_alive.store(false, std::memory_order_release);
    host.width = 0;
    host.height = 0;

    SafeReleaseWindow(mode);

    host.last_egl_context = EGL_NO_CONTEXT;
    host.render_thread.store(0, std::memory_order_release);
    host.initialized.store(false, std::memory_order_release);
}

// 重置失败计数，调用方需已持有锁
void ResetFailureStateLocked(const char* reason) {
    if (host.consecutive_failures > 0) {
        NYX_LOGI("glsurface failure state reset: reason=%s failures=%d",
                 reason ? reason : "unknown", host.consecutive_failures);
    }
    host.consecutive_failures = 0;
    host.first_failure_ms = 0;
}

} // namespace

// 绑定 Java Surface
bool Attach(JNIEnv* env, jobject surface) {
    if (env == nullptr || surface == nullptr) {
        std::lock_guard<std::mutex> lock(host_mutex);
        ClearState(CleanupMode::NormalSurfaceDestroyed);
        NYX_LOGW("glsurface attach skipped: null surface");
        return false;
    }

    ANativeWindow* next_window = ANativeWindow_fromSurface(env, surface);
    std::lock_guard<std::mutex> lock(host_mutex);

    // 已有旧状态时强制清理，避免触碰可能悬空的旧 window 指针
    if (host.initialized.load(std::memory_order_acquire)) {
        NYX_LOGW("glsurface re-attach: clearing old state old_gen=%llu new_window=%p",
                 static_cast<unsigned long long>(host.generation.load()),
                 next_window);
        ClearState(CleanupMode::ForcedSurfaceRecreate);
    } else {
        SafeReleaseWindow(CleanupMode::NormalSurfaceDestroyed);
    }

    if (next_window == nullptr) {
        NYX_LOGE("glsurface attach failed: ANativeWindow_fromSurface returned null");
        return false;
    }

    host.window = next_window;

    // attach 时先校验 native window 尺寸
    int32_t w = ANativeWindow_getWidth(next_window);
    int32_t h = ANativeWindow_getHeight(next_window);
    if (w <= 0 || h <= 0) {
        NYX_LOGE("glsurface attach failed: invalid window size %dx%d", w, h);
        ANativeWindow_release(next_window);
        host.window = nullptr;
        return false;
    }
    host.width = w;
    host.height = h;

    // EGL Context 在 onDrawFrame 时自动绑定，只有 GLThread 才有有效 Context
    host.last_egl_context = EGL_NO_CONTEXT;
    host.render_thread.store(0, std::memory_order_release);

    host.initialized.store(true, std::memory_order_release);
    host.surface_alive.store(true, std::memory_order_release);
    host.recreate_view_pending.store(false, std::memory_order_release);
    host.generation.fetch_add(1, std::memory_order_acq_rel);

    ResetFailureStateLocked("attach");

    NYX_LOGI("glsurface attached: generation=%llu window=%p size=%dx%d",
             static_cast<unsigned long long>(host.generation.load()),
             host.window, host.width, host.height);
    return true;
}

// 更新 Surface 尺寸
void Resize(int width, int height, float density) {
    std::lock_guard<std::mutex> lock(host_mutex);
    if (width <= 0 || height <= 0) {
        host.width = width;
        host.height = height;
        host.surface_alive.store(false, std::memory_order_release);
        NYX_LOGW("glsurface invalid resize: %dx%d", width, height);
        return;
    }

    host.width = width;
    host.height = height;
    host.density = density > 0.0f ? density : 1.0f;
    host.surface_alive.store(true, std::memory_order_release);
    NYX_LOGI("glsurface resized: %dx%d density=%.2f", width, height, host.density);
}

// 解绑 Surface
void Detach() {
    std::lock_guard<std::mutex> lock(host_mutex);
    ClearState(CleanupMode::NormalSurfaceDestroyed);
    NYX_LOGI("glsurface detached");
}

// 关闭 Surface Host
void Shutdown() {
    std::lock_guard<std::mutex> lock(host_mutex);
    ClearState(CleanupMode::RuntimeUnload);
    host.density = 1.0f;
    NYX_LOGI("glsurface shutdown");
}

// 获取 Surface 快照
SurfaceSnapshot Snapshot() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return SurfaceSnapshot{host.window, host.width, host.height, host.density};
}

// 判断 Surface 是否可用
bool IsReady() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return host.window != nullptr && host.width > 0 && host.height > 0;
}

// 轻量校验，每帧调用，不触碰 ANativeWindow
bool ValidateSurfaceFast(const char* caller) {
    if (!host.initialized.load(std::memory_order_acquire) ||
        !host.surface_alive.load(std::memory_order_acquire)) {
        return false;
    }

    EGLContext current_context = eglGetCurrentContext();
    if (current_context == EGL_NO_CONTEXT) {
        return false;
    }

    std::lock_guard<std::mutex> lock(host_mutex);
    if (host.window == nullptr || host.width <= 0 || host.height <= 0) {
        return false;
    }

    return true;
}

// 重量校验，检查 EGL Context 和窗口尺寸
bool ValidateSurface(const char* caller) {
    if (!host.surface_alive.load(std::memory_order_acquire)) {
        NYX_LOGW("glsurface %s: surface not alive", caller);
        return false;
    }

    std::lock_guard<std::mutex> lock(host_mutex);

    if (host.window == nullptr) {
        NYX_LOGW("glsurface %s: window is null", caller);
        return false;
    }

    EGLContext current_context = eglGetCurrentContext();
    if (current_context == EGL_NO_CONTEXT) {
        NYX_LOGW("glsurface %s: EGL context invalid", caller);
        return false;
    }

    int32_t w = ANativeWindow_getWidth(host.window);
    int32_t h = ANativeWindow_getHeight(host.window);
    if (w <= 0 || h <= 0) {
        NYX_LOGW("glsurface %s: invalid window size %dx%d", caller, w, h);
        ClearState(CleanupMode::ForcedSurfaceRecreate);
        return false;
    }

    if (w != host.width || h != host.height) {
        host.width = w;
        host.height = h;
        NYX_LOGD("glsurface %s: size updated to %dx%d", caller, w, h);
    }

    return true;
}

// 检查并绑定 EGL Context
bool CheckAndUpdateEGLContext() {
    if (!host.surface_alive.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(host_mutex);

    if (host.window == nullptr) {
        return false;
    }

    pthread_t current_thread = pthread_self();
    pthread_t expected_thread = host.render_thread.load(std::memory_order_acquire);

    if (expected_thread != 0 && !pthread_equal(current_thread, expected_thread)) {
        const uint64_t generation = host.generation.load(std::memory_order_acquire);
        uint64_t logged_generation =
            host.stale_thread_logged_generation.load(std::memory_order_acquire);
        if (logged_generation != generation &&
            host.stale_thread_logged_generation.compare_exchange_strong(
                logged_generation,
                generation,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            NYX_LOGD("glsurface stale render thread skipped: generation=%llu current=%lu expected=%lu",
                     static_cast<unsigned long long>(generation),
                     static_cast<unsigned long>(current_thread),
                     static_cast<unsigned long>(expected_thread));
        }
        return false;
    }

    EGLContext current_context = eglGetCurrentContext();
    if (current_context == EGL_NO_CONTEXT) {
        return false;
    }

    // 检测 EGL Context 是否发生变化
    if (host.last_egl_context != EGL_NO_CONTEXT && current_context != host.last_egl_context) {
        NYX_LOGW("glsurface EGL context mismatch: expected=%p current=%p",
                 host.last_egl_context, current_context);
        return false;
    }

    // 首次绑定渲染线程和 EGL Context
    if (host.last_egl_context == EGL_NO_CONTEXT) {
        host.last_egl_context = current_context;
        host.render_thread.store(pthread_self(), std::memory_order_release);
        NYX_LOGI("glsurface EGL context first bind: generation=%llu context=%p thread=%lu",
                 static_cast<unsigned long long>(host.generation.load()),
                 current_context, (unsigned long)pthread_self());
    }

    return true;
}

// 当前线程是否为渲染线程
bool IsCurrentThreadRenderThread() {
    pthread_t current = pthread_self();
    pthread_t expected = host.render_thread.load(std::memory_order_acquire);
    return expected != 0 && pthread_equal(current, expected);
}

// 当前线程是否是上一代 GLSurfaceView 遗留的 GLThread
bool IsStaleRenderThread() {
    pthread_t current = pthread_self();
    pthread_t expected = host.render_thread.load(std::memory_order_acquire);
    return expected != 0 && !pthread_equal(current, expected);
}

// 获取 Surface generation
uint64_t GetGeneration() {
    return host.generation.load(std::memory_order_acquire);
}

// 请求重建 GLSurfaceView
bool RequestRecreateView(const char* reason) {
    if (host.recreate_view_pending.load(std::memory_order_acquire)) {
        NYX_LOGI("glsurface recreate already pending: reason=%s", reason ? reason : "unknown");
        return true;
    }

    const int64_t now = NowMs();
    const int64_t last = host.last_recreate_ms.load(std::memory_order_acquire);
    if (last > 0 && now - last < kRecreateDebounceMs) {
        NYX_LOGI("glsurface recreate debounced: reason=%s elapsed=%lldms",
                 reason ? reason : "unknown", static_cast<long long>(now - last));
        return false;
    }

    bool expected = false;
    if (!host.recreate_view_pending.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
        NYX_LOGI("glsurface recreate already pending: reason=%s", reason ? reason : "unknown");
        return true;
    }

    host.last_recreate_ms.store(now, std::memory_order_release);
    NYX_LOGI("glsurface requesting recreate view: reason=%s", reason ? reason : "unknown");
    return true;
}

// 获取清理模式名称
const char* CleanupModeName(CleanupMode mode) {
    return CleanupModeNameInternal(mode);
}

// 校验失败后按连续失败和防抖策略请求重建
void MaybeRequestRecreate(const char* reason) {
    std::lock_guard<std::mutex> lock(host_mutex);

    const int64_t now = NowMs();
    if (host.consecutive_failures == 0) {
        host.first_failure_ms = now;
    }
    ++host.consecutive_failures;

    const int64_t elapsed = host.first_failure_ms > 0 ? now - host.first_failure_ms : 0;
    if (host.consecutive_failures < kRecreateAfterConsecutiveFailures &&
        elapsed < kRecreateAfterFailureDurationMs) {
        NYX_LOGD("glsurface failure deferred: reason=%s failures=%d elapsed=%lldms",
                 reason ? reason : "unknown", host.consecutive_failures,
                 static_cast<long long>(elapsed));
        return;
    }

    NYX_LOGW("glsurface failure persisted, requesting recreate: reason=%s failures=%d elapsed=%lldms",
             reason ? reason : "unknown", host.consecutive_failures,
             static_cast<long long>(elapsed));
    ResetFailureStateLocked("request recreate");
    if (RequestRecreateView(reason)) {
        ClearState(CleanupMode::ForcedSurfaceRecreate);
    }
}

// 重置失败计数
void ResetFailureState(const char* reason) {
    std::lock_guard<std::mutex> lock(host_mutex);
    ResetFailureStateLocked(reason);
}

} // namespace glsurface
} // namespace overlay
} // namespace nyx
