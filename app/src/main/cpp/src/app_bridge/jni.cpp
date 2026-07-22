#include "src/app_bridge/jni.h"

#include "sdk/include/app.h"
#include "sdk/include/utils.h"

#include <atomic>

namespace {

// JNI_OnLoad 写入一次，SDK app 接口按 acquire 读取。
std::atomic<JavaVM*> g_java_vm{nullptr};

void set_java_vm(JavaVM* vm) {
    g_java_vm.store(vm, std::memory_order_release);
}

} // namespace

namespace nyx {
namespace sdk {
namespace app {

// 返回进程级 JavaVM。
JavaVM* GetJavaVM() {
    return g_java_vm.load(std::memory_order_acquire);
}

} // namespace app
} // namespace sdk
} // namespace nyx

// 模块可选 JNI 注册入口，未导出时跳过。
extern "C" bool NyxModRegisterJni(JNIEnv* env) __attribute__((weak));

namespace {

bool register_mod_jni(JNIEnv* env) {
    if (NyxModRegisterJni == nullptr) {
        return true;
    }
    return NyxModRegisterJni(env);
}

} // namespace

namespace nyx {
namespace app_bridge {

// 统一处理 class 查找、optional 桥接和 RegisterNatives。
bool register_methods(
    JNIEnv* env,
    const char* class_name,
    const JNINativeMethod* methods,
    int count,
    bool optional
) {
    if (env == nullptr || class_name == nullptr || methods == nullptr || count <= 0) {
        return false;
    }

    jclass clazz = env->FindClass(class_name);
    if (clazz == nullptr) {
        env->ExceptionClear();
        if (optional) {
            NYX_LOGI("optional JNI class not found: %s", class_name);
            return true;
        }

        NYX_LOGE("JNI class not found: %s", class_name);
        return false;
    }

    const jint result = env->RegisterNatives(clazz, methods, count);
    env->DeleteLocalRef(clazz);
    if (result != JNI_OK) {
        NYX_LOGE("JNI register failed: %s", class_name);
        return false;
    }

    return true;
}

} // namespace app_bridge
} // namespace nyx

// app bridge 的 JNI 注册入口。
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    if (vm == nullptr) {
        return JNI_ERR;
    }

    set_java_vm(vm);

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        return JNI_ERR;
    }

    if (!nyx::app_bridge::register_surface(env) ||
        !nyx::app_bridge::register_auth(env) ||
        !nyx::app_bridge::register_tests(env) ||
        !register_mod_jni(env)) {
        return JNI_ERR;
    }

    NYX_LOGI("app bridge JNI registered");
    return JNI_VERSION_1_6;
}
