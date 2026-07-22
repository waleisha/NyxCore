#include <jni.h>

#include "sdk/include/auth.h"
#include "sdk/include/test.h"
#include "src/app_bridge/auth_config.h"
#include "src/app_bridge/jni.h"
#include "src/app_bridge/runtime.h"

namespace nyx {
namespace sdk {
namespace test {

#if NYX_ENABLE_INTEGRATION_GATES
bool CheckNetIntegration(const char* url);
bool CheckAuthIntegration(const char* license, const char* var_key);
#endif

} // namespace test
} // namespace sdk
} // namespace nyx

namespace {

// androidTest 侧可选 NativeTestBridge 类名。
const char* k_native_test_bridge_class = "dev/nyxcore/manager/NativeTestBridge";

#if NYX_ENABLE_INTEGRATION_GATES
// 运行发布前集成门禁。
jboolean run_release_gate(JNIEnv* /* env */, jobject /* thiz */) {
    nyx::app_bridge::init_runtime();
    return nyx::sdk::test::CheckRelease() ? JNI_TRUE : JNI_FALSE;
}

// 运行网络集成门禁。
jboolean run_network_gate(
    JNIEnv* env,
    jobject /* thiz */,
    jstring url
) {
    nyx::app_bridge::init_runtime();

    if (url == nullptr) {
        return nyx::sdk::test::CheckNetIntegration(nullptr) ? JNI_TRUE : JNI_FALSE;
    }

    const std::string value = nyx::app_bridge::string_from(env, url);
    return nyx::sdk::test::CheckNetIntegration(value.c_str()) ? JNI_TRUE : JNI_FALSE;
}

// 运行授权集成门禁。
jboolean run_auth_gate(
    JNIEnv* env,
    jobject /* thiz */,
    jstring license,
    jstring var_key
) {
    nyx::app_bridge::init_runtime();

    const std::string license_value = nyx::app_bridge::string_from(env, license);
    const std::string key_value = nyx::app_bridge::string_from(env, var_key);
    return nyx::sdk::test::CheckAuthIntegration(
        license_value.c_str(),
        key_value.empty() ? nullptr : key_value.c_str()
    ) ? JNI_TRUE : JNI_FALSE;
}

// 使用 Android Context 初始化后运行授权集成门禁。
jboolean run_auth_gate_with_context(
    JNIEnv* env,
    jobject /* thiz */,
    jobject context,
    jstring license,
    jstring var_key
) {
    nyx::app_bridge::init_runtime();

    const std::string license_value = nyx::app_bridge::string_from(env, license);
    const std::string key_value = nyx::app_bridge::string_from(env, var_key);
    auto config = nyx::app_bridge::default_auth_config();
    const auto init = nyx::sdk::auth::Init(env, context, config);
    if (!init.success) {
        NYX_LOGE("auth integration context init failed: %s", init.message.c_str());
        return JNI_FALSE;
    }

    const bool passed = nyx::sdk::test::CheckAuthIntegration(
        license_value.c_str(),
        key_value.empty() ? nullptr : key_value.c_str()
    );
    return passed ? JNI_TRUE : JNI_FALSE;
}

// NativeTestBridge 的集成测试 JNI 表。
const JNINativeMethod k_test_methods[] = {
    {"nativeCheckRelease", "()Z", reinterpret_cast<void*>(run_release_gate)},
    {"nativeCheckNetIntegration", "(Ljava/lang/String;)Z", reinterpret_cast<void*>(run_network_gate)},
    {
        "nativeCheckAuthIntegration",
        "(Ljava/lang/String;Ljava/lang/String;)Z",
        reinterpret_cast<void*>(run_auth_gate),
    },
    {
        "nativeCheckAuthIntegrationWithContext",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        reinterpret_cast<void*>(run_auth_gate_with_context),
    },
};
#endif

} // namespace

namespace nyx {
namespace app_bridge {

// 注册集成测试 JNI 方法；门禁关闭时不注册任何 native 方法。
bool register_tests(JNIEnv* env) {
#if NYX_ENABLE_INTEGRATION_GATES
    return register_methods(
        env,
        k_native_test_bridge_class,
        k_test_methods,
        static_cast<int>(sizeof(k_test_methods) / sizeof(k_test_methods[0])),
        true
    );
#else
    (void)env;
    return true;
#endif
}

} // namespace app_bridge
} // namespace nyx
