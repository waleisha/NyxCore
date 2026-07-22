#pragma once

#include <jni.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include <EGL/egl.h>
#include <android/native_window.h>
#include <pthread.h>

namespace nyx {
namespace overlay {
namespace glsurface {

// Surface 清理场景
enum class CleanupMode {
    // 正常 Surface 销毁
    NormalSurfaceDestroyed,
    // 强制 Surface 重建
    ForcedSurfaceRecreate,
    // runtime 卸载
    RuntimeUnload,
};

// Surface 状态快照
struct SurfaceSnapshot {
    // Native window 指针
    ANativeWindow* window = nullptr;
    // Surface 宽度
    int width = 0;
    // Surface 高度
    int height = 0;
    // 屏幕密度
    float density = 1.0f;
};

// 绑定 Java Surface
bool Attach(JNIEnv* env, jobject surface);
// 更新 Surface 尺寸
void Resize(int width, int height, float density);
// 解绑 Surface
void Detach();
// 关闭 Surface Host
void Shutdown();

// 获取 Surface 快照
SurfaceSnapshot Snapshot();
// 判断 Surface 是否可用
bool IsReady();

// 轻量校验，每帧调用，不触碰 ANativeWindow
bool ValidateSurfaceFast(const char* caller);

// 重量校验，检查 EGL Context 和窗口尺寸
bool ValidateSurface(const char* caller);

// 检查并绑定 EGL Context
bool CheckAndUpdateEGLContext();

// 当前线程是否为渲染线程
bool IsCurrentThreadRenderThread();

// 当前线程是否是上一代 GLSurfaceView 遗留的 GLThread
bool IsStaleRenderThread();

// 获取 Surface generation
uint64_t GetGeneration();

// 请求重建 GLSurfaceView
bool RequestRecreateView(const char* reason);

// 校验失败后按连续失败和防抖策略请求重建
void MaybeRequestRecreate(const char* reason);
// 重置失败计数
void ResetFailureState(const char* reason);

// 获取清理模式名称
const char* CleanupModeName(CleanupMode mode);

} // namespace glsurface
} // namespace overlay
} // namespace nyx
