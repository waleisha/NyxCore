#include <jni.h>

#include "src/app_bridge/android_context.h"
#include "src/app_bridge/jni.h"
#include "sdk/include/utils.h"
#include "src/app_bridge/runtime.h"
#include "src/core/context.h"
#include "src/overlay/glsurface/glsurface_host.h"
#include "src/overlay/imgui_bind/imgui_bridge.h"
#include "src/overlay/input/input_router.h"

#include <string>
#include <vector>

namespace {

// Kotlin NativeBridge 类名。
const char* k_native_bridge_class = "dev/nyxcore/manager/NativeBridge";

// 软键盘控制由 GLSurfaceView 暴露的静态方法承接。
const char* k_gl_surface_view_class = "dev/nyxcore/manager/overlay/NyxGLSurfaceView";
jclass g_gl_surface_view = nullptr;
jmethodID g_show_input = nullptr;
jmethodID g_hide_input = nullptr;

// ImGui 文本输入状态，避免每帧重复拉起或隐藏输入法。
bool g_want_text_input_last = false;
bool g_text_input_touch_request = false;

// 缓存软键盘桥接类和静态方法。
bool cache_keyboard_bridge(JNIEnv* env) {
    if (env == nullptr) {
        return false;
    }

    if (g_gl_surface_view != nullptr && g_show_input != nullptr && g_hide_input != nullptr) {
        return true;
    }

    jclass local_class = env->FindClass(k_gl_surface_view_class);
    if (local_class == nullptr) {
        env->ExceptionClear();
        NYX_LOGE("JNI class not found: %s", k_gl_surface_view_class);
        return false;
    }

    jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
    env->DeleteLocalRef(local_class);
    if (global_class == nullptr) {
        NYX_LOGE("JNI global ref failed: %s", k_gl_surface_view_class);
        return false;
    }

    jmethodID show_method = env->GetStaticMethodID(global_class, "showInputUI", "()V");
    jmethodID hide_method = env->GetStaticMethodID(global_class, "hideInputUI", "()V");
    if (show_method == nullptr || hide_method == nullptr) {
        env->ExceptionClear();
        env->DeleteGlobalRef(global_class);
        NYX_LOGE("JNI keyboard bridge methods not found");
        return false;
    }

    g_gl_surface_view = global_class;
    g_show_input = show_method;
    g_hide_input = hide_method;
    return true;
}

// 调用 Kotlin 层显示或隐藏输入法。
bool call_keyboard_bridge(JNIEnv* env, bool show) {
    if (!cache_keyboard_bridge(env)) {
        return false;
    }

    env->CallStaticVoidMethod(g_gl_surface_view, show ? g_show_input : g_hide_input);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        NYX_LOGW("JNI keyboard bridge call failed: show=%d", show ? 1 : 0);
        return false;
    }

    return true;
}

// 重置文本输入同步状态。
void reset_keyboard_state() {
    g_want_text_input_last = false;
    g_text_input_touch_request = false;
}

// 按 ImGui 输入需求同步软键盘状态。
void sync_text_input(JNIEnv* env, bool want_text_input) {
    if (want_text_input && (!g_want_text_input_last || g_text_input_touch_request)) {
        call_keyboard_bridge(env, true);
        g_text_input_touch_request = false;
    } else if (!want_text_input && g_want_text_input_last) {
        call_keyboard_bridge(env, false);
        g_text_input_touch_request = false;
    }

    if (!want_text_input) {
        g_text_input_touch_request = false;
    }

    g_want_text_input_last = want_text_input;
}

// 绑定 native 主线程上下文。
void bind_main_thread(JNIEnv* /* env */, jobject /* thiz */) {
    nyx::core::Context::instance().bind_main_thread();
}

// Surface 创建后初始化 GL、ImGui 和 native runtime。
void surface_created(
    JNIEnv* env,
    jobject /* thiz */,
    jobject surface,
    jobject /* gl */,
    jobject /* config */
) {
    if (!nyx::overlay::glsurface::Attach(env, surface)) {
        return;
    }
    nyx::overlay::input::Clear();
    reset_keyboard_state();

    nyx::app_bridge::init_runtime();
    nyx::app_bridge::set_shutdown(false);
    const auto surface_snapshot = nyx::overlay::glsurface::Snapshot();
    if (!nyx::overlay::imgui_bind::Init(surface_snapshot.window)) {
        NYX_LOGE("native surface created: imgui init failed");
        return;
    }

    NYX_LOGI("native surface created");
}

// 同步 Surface 尺寸到 GL 和 ImGui。
void surface_changed(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jobject /* gl */,
    jint width,
    jint height
) {
    nyx::overlay::glsurface::Resize(width, height, 1.0f);
    nyx::overlay::imgui_bind::Resize(width, height, 1.0f);
    NYX_LOGI("native surface changed");
}

// 驱动一帧 ImGui，并同步软键盘状态。
void draw_frame(JNIEnv* env, jobject /* thiz */, jobject /* gl */) {
    if (!nyx::app_bridge::is_running()) {
        return;
    }

    nyx::overlay::imgui_bind::Frame();
    sync_text_input(env, nyx::overlay::input::WantsTextInput());
}

// 转发触摸事件到 overlay 输入路由。
jboolean touch(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jfloat x,
    jfloat y,
    jint action
) {
    const bool success = nyx::overlay::input::PushTouch(x, y, action);
    if (success && action == 0) {
        g_text_input_touch_request = true;
    }
    return success ? JNI_TRUE : JNI_FALSE;
}

// 转发文本输入到 overlay 输入路由。
void text_input(JNIEnv* env, jobject /* thiz */, jstring text) {
    const std::string value = nyx::app_bridge::string_from(env, text);
    nyx::overlay::input::PushText(value.c_str());
}

// 转发退格输入到 overlay 输入路由。
void backspace(JNIEnv* /* env */, jobject /* thiz */) {
    nyx::overlay::input::PushBackspace();
}

// 返回 ImGui 窗口矩形，Kotlin 侧据此创建触摸代理窗口。
jfloatArray imgui_window_bounds(JNIEnv* env, jobject /* thiz */) {
    const auto rects = nyx::overlay::input::CaptureRects();
    const auto value_count = static_cast<jsize>(rects.size() * 4);
    jfloatArray result = env->NewFloatArray(value_count);
    if (result == nullptr || value_count == 0) {
        return result;
    }

    std::vector<jfloat> values;
    values.reserve(static_cast<std::size_t>(value_count));
    for (const auto& rect : rects) {
        values.push_back(rect.left);
        values.push_back(rect.top);
        values.push_back(rect.right);
        values.push_back(rect.bottom);
    }

    env->SetFloatArrayRegion(result, 0, value_count, values.data());
    return result;
}

// Surface 销毁时释放 GL/ImGui 侧资源。
void surface_destroyed(JNIEnv* env, jobject /* thiz */, jobject /* surface */) {
    sync_text_input(env, false);
    reset_keyboard_state();
    nyx::overlay::imgui_bind::Shutdown("surface destroyed");
    nyx::overlay::input::Clear();
    nyx::overlay::glsurface::Detach();
    NYX_LOGI("native surface destroyed");
}

// Kotlin 侧主动关闭 native runtime 和 overlay。
void shutdown(JNIEnv* env, jobject /* thiz */) {
    nyx::app_bridge::set_shutdown(true);
    nyx::app_bridge::clear_android_context(env);
    sync_text_input(env, false);
    reset_keyboard_state();
    nyx::overlay::imgui_bind::Shutdown("native shutdown");
    nyx::overlay::input::Clear();
    nyx::overlay::glsurface::Shutdown();
    NYX_LOGI("native shutdown");
}

// Kotlin NativeBridge 的渲染和输入 JNI 表。
const JNINativeMethod k_surface_methods[] = {
    {"nativeBindMainThread", "()V", reinterpret_cast<void*>(bind_main_thread)},
    {"nativeOnSurfaceCreated", "(Landroid/view/Surface;Ljavax/microedition/khronos/opengles/GL10;Ljavax/microedition/khronos/egl/EGLConfig;)V", reinterpret_cast<void*>(surface_created)},
    {"nativeOnSurfaceChanged", "(Ljavax/microedition/khronos/opengles/GL10;II)V", reinterpret_cast<void*>(surface_changed)},
    {"nativeOnDrawFrame", "(Ljavax/microedition/khronos/opengles/GL10;)V", reinterpret_cast<void*>(draw_frame)},
    {"nativeOnTouch", "(FFI)Z", reinterpret_cast<void*>(touch)},
    {"nativeOnTextInput", "(Ljava/lang/String;)V", reinterpret_cast<void*>(text_input)},
    {"nativeOnBackspace", "()V", reinterpret_cast<void*>(backspace)},
    {"nativeGetImGuiWindowBounds", "()[F", reinterpret_cast<void*>(imgui_window_bounds)},
    {"nativeOnSurfaceDestroyed", "(Landroid/view/Surface;)V", reinterpret_cast<void*>(surface_destroyed)},
    {"nativeShutdown", "()V", reinterpret_cast<void*>(shutdown)},
};

} // namespace

namespace nyx {
namespace app_bridge {

// 注册 Surface 与输入相关 JNI 方法。
bool register_surface(JNIEnv* env) {
    if (!register_methods(
        env,
        k_native_bridge_class,
        k_surface_methods,
        static_cast<int>(sizeof(k_surface_methods) / sizeof(k_surface_methods[0]))
    )) {
        return false;
    }

    return cache_keyboard_bridge(env);
}

} // namespace app_bridge
} // namespace nyx
