#include "src/app_bridge/android_context.h"

#include "sdk/include/app.h"
#include "sdk/include/utils.h"

#include <android/asset_manager_jni.h>

#include <mutex>

namespace {

// Android 侧全局对象缓存。
struct AndroidContextState {
    // 进程级 JavaVM。
    JavaVM* vm = nullptr;

    // Application Context 的 global ref。
    jobject app_context = nullptr;

    // AssetManager Java 对象的 global ref。
    jobject asset_manager_object = nullptr;

    // native AssetManager 指针。
    AAssetManager* asset_manager = nullptr;
};

std::mutex g_context_mutex;
AndroidContextState g_context;

// 需要清理 global ref 时，给当前线程补齐 JNIEnv。
class EnvGuard {
public:
    explicit EnvGuard(JavaVM* vm) : vm_(vm) {
        if (vm_ == nullptr) {
            return;
        }

        if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_EDETACHED &&
            vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
            attached_ = true;
        }
    }

    ~EnvGuard() {
        if (attached_ && vm_ != nullptr) {
            vm_->DetachCurrentThread();
        }
    }

    JNIEnv* get() const {
        return env_;
    }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

// 清除 JNI 异常并把异常状态返回给调用方。
bool clear_exception(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

// 优先提取 Application Context，失败时返回空。
jobject app_context(JNIEnv* env, jobject context) {
    if (env == nullptr || context == nullptr) {
        return nullptr;
    }

    jclass context_class = env->GetObjectClass(context);
    if (context_class == nullptr || clear_exception(env)) {
        return nullptr;
    }

    jmethodID get_application_context = env->GetMethodID(
        context_class,
        "getApplicationContext",
        "()Landroid/content/Context;"
    );
    env->DeleteLocalRef(context_class);
    if (get_application_context == nullptr || clear_exception(env)) {
        return nullptr;
    }

    jobject app = env->CallObjectMethod(context, get_application_context);
    if (clear_exception(env)) {
        return nullptr;
    }
    return app != nullptr ? app : context;
}

// 从 Context 中提取 AssetManager。
jobject assets(JNIEnv* env, jobject context) {
    if (env == nullptr || context == nullptr) {
        return nullptr;
    }

    jclass context_class = env->GetObjectClass(context);
    if (context_class == nullptr || clear_exception(env)) {
        return nullptr;
    }

    jmethodID get_assets = env->GetMethodID(
        context_class,
        "getAssets",
        "()Landroid/content/res/AssetManager;"
    );
    env->DeleteLocalRef(context_class);
    if (get_assets == nullptr || clear_exception(env)) {
        return nullptr;
    }

    jobject manager = env->CallObjectMethod(context, get_assets);
    if (clear_exception(env)) {
        return nullptr;
    }
    return manager;
}

// 在持锁状态下释放缓存的 Java global ref。
void clear_locked(JNIEnv* env) {
    if (env == nullptr || (g_context.app_context == nullptr && g_context.asset_manager_object == nullptr)) {
        g_context = AndroidContextState{};
        return;
    }

    if (g_context.asset_manager_object != nullptr) {
        env->DeleteGlobalRef(g_context.asset_manager_object);
    }
    if (g_context.app_context != nullptr) {
        env->DeleteGlobalRef(g_context.app_context);
    }
    g_context = AndroidContextState{};
}

} // namespace

namespace nyx {
namespace app_bridge {

// 缓存 Android Context 及 AssetManager，给 SDK app 接口读取。
bool set_android_context(JNIEnv* env, jobject context) {
    if (env == nullptr || context == nullptr) {
        return false;
    }

    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
        return false;
    }

    jobject local_app = app_context(env, context);
    if (local_app == nullptr) {
        return false;
    }

    jobject local_assets = assets(env, local_app);
    jobject global_app = env->NewGlobalRef(local_app);
    jobject global_assets = local_assets != nullptr ? env->NewGlobalRef(local_assets) : nullptr;
    AAssetManager* asset_manager = global_assets != nullptr
        ? AAssetManager_fromJava(env, global_assets)
        : nullptr;

    if (local_app != context) {
        env->DeleteLocalRef(local_app);
    }
    if (local_assets != nullptr) {
        env->DeleteLocalRef(local_assets);
    }

    if (global_app == nullptr) {
        if (global_assets != nullptr) {
            env->DeleteGlobalRef(global_assets);
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(g_context_mutex);
    clear_locked(env);
    g_context.vm = vm;
    g_context.app_context = global_app;
    g_context.asset_manager_object = global_assets;
    g_context.asset_manager = asset_manager;

    if (asset_manager == nullptr) {
        NYX_LOGW("Android AssetManager unavailable for native runtime");
    } else {
        NYX_LOGI("Android context cached for native runtime");
    }
    return true;
}

// 清理 Android Context 缓存；无 env 时临时附加到 VM。
void clear_android_context(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    if (env != nullptr) {
        clear_locked(env);
        return;
    }

    EnvGuard guard(g_context.vm);
    clear_locked(guard.get());
}

} // namespace app_bridge
} // namespace nyx

namespace nyx {
namespace sdk {
namespace app {

// 返回缓存的 Application Context local ref。
jobject GetApplicationContext(JNIEnv* env) {
    if (env == nullptr) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_context_mutex);
    return g_context.app_context != nullptr
        ? env->NewLocalRef(g_context.app_context)
        : nullptr;
}

// 返回缓存的 native AssetManager 指针。
AAssetManager* GetAssetManager() {
    std::lock_guard<std::mutex> lock(g_context_mutex);
    return g_context.asset_manager;
}

} // namespace app
} // namespace sdk
} // namespace nyx
