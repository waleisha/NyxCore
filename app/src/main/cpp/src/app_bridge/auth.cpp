#include <jni.h>

#include "sdk/include/auth.h"
#include "src/app_bridge/android_context.h"
#include "src/app_bridge/auth_config.h"
#include "src/app_bridge/jni.h"

#include <mutex>
#include <string>
#include <utility>

// 模块侧可选授权扩展入口。
extern "C" bool NyxModInstallPayloadSeedFromAuth() __attribute__((weak));
extern "C" void NyxModClearPayloadSeed() __attribute__((weak));
extern "C" bool NyxModCanRunAuthRuntime(const char* feature) __attribute__((weak));

namespace nyx {
namespace core {
namespace auth {

bool is_ready();

} // namespace auth
} // namespace core
} // namespace nyx

namespace {

// 最近一次授权调用状态，供 Kotlin 侧查询和展示。
struct AuthState {
    // Android 授权环境是否已配置完成。
    bool configured = false;

    // 最近一次授权操作名。
    std::string operation = "idle";

    // 最近一次授权结果。
    nyx::sdk::auth::Result result;

    // 最近一次附带文本值。
    std::string value;
};

std::mutex g_auth_mutex;
AuthState g_auth_state;

int failure_value(nyx::sdk::auth::Err failure) {
    return static_cast<int>(failure);
}

bool configured() {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    return g_auth_state.configured;
}

jstring new_jstring(JNIEnv* env, const std::string& value) {
    if (env == nullptr) {
        return nullptr;
    }
    return env->NewStringUTF(value.c_str());
}

// 构造 Kotlin AuthResult 对象。
jobject auth_result_object(
    JNIEnv* env,
    jclass result_class,
    const std::string& operation,
    const nyx::sdk::auth::Result& result,
    const std::string& value
) {
    if (env == nullptr || result_class == nullptr) {
        return nullptr;
    }

    jmethodID constructor = env->GetMethodID(
        result_class,
        "<init>",
        "(Ljava/lang/String;ZIILjava/lang/String;Ljava/lang/String;ZZ)V"
    );
    if (constructor == nullptr) {
        env->ExceptionClear();
        return nullptr;
    }

    jstring j_operation = new_jstring(env, operation);
    jstring j_message = new_jstring(env, result.message);
    jstring j_value = new_jstring(env, value);
    if (j_operation == nullptr || j_message == nullptr || j_value == nullptr) {
        if (j_operation != nullptr) {
            env->DeleteLocalRef(j_operation);
        }
        if (j_message != nullptr) {
            env->DeleteLocalRef(j_message);
        }
        if (j_value != nullptr) {
            env->DeleteLocalRef(j_value);
        }
        return nullptr;
    }

    jobject out = env->NewObject(
        result_class,
        constructor,
        j_operation,
        static_cast<jboolean>(result.success),
        static_cast<jint>(result.code),
        static_cast<jint>(failure_value(result.failure)),
        j_message,
        j_value,
        static_cast<jboolean>(configured()),
        static_cast<jboolean>(nyx::sdk::auth::IsLoggedIn())
    );
    env->DeleteLocalRef(j_operation);
    env->DeleteLocalRef(j_message);
    env->DeleteLocalRef(j_value);
    return out;
}

// 记录最近一次授权调用结果。
void record(
    const std::string& operation,
    const nyx::sdk::auth::Result& result,
    const std::string& value = {}
) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_auth_state.operation = operation;
    g_auth_state.result = result;
    g_auth_state.value = value;
}

// 创建成功授权结果。
nyx::sdk::auth::Result ok(std::string message) {
    nyx::sdk::auth::Result result;
    result.success = true;
    result.failure = nyx::sdk::auth::Err::None;
    result.message = std::move(message);
    return result;
}

// 创建本地状态失败结果。
nyx::sdk::auth::Result fail(std::string message) {
    nyx::sdk::auth::Result result;
    result.code = -3001;
    result.failure = nyx::sdk::auth::Err::LocalState;
    result.message = std::move(message);
    return result;
}

// 创建指定错误码的授权失败结果。
nyx::sdk::auth::Result fail(
    int code,
    nyx::sdk::auth::Err failure,
    std::string message
) {
    nyx::sdk::auth::Result result;
    result.code = code;
    result.failure = failure;
    result.message = std::move(message);
    return result;
}

// 授权状态变化时清理模块侧 payload seed。
void clear_mod_payload_seed() {
    if (NyxModClearPayloadSeed != nullptr) {
        NyxModClearPayloadSeed();
    }
}

const char* k_native_bridge_class = "dev/nyxcore/manager/NativeBridge";

// 初始化授权环境并缓存 Android Context。
jobject configure_auth(
    JNIEnv* env,
    jobject /* thiz */,
    jobject context,
    jclass result_class
) {
    nyx::app_bridge::set_android_context(env, context);
    auto config = nyx::app_bridge::default_auth_config();
    const auto result = nyx::sdk::auth::Init(env, context, config);
    const bool ready = result.success && nyx::core::auth::is_ready();
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        g_auth_state.configured = ready;
    }

    record("configure", result);
    return auth_result_object(env, result_class, "configure", result, {});
}

// 返回最近一次授权调用状态。
jobject auth_status(JNIEnv* env, jobject /* thiz */, jclass result_class) {
    AuthState state;
    {
        std::lock_guard<std::mutex> lock(g_auth_mutex);
        state = g_auth_state;
    }

    return auth_result_object(env, result_class, "status", state.result, state.value);
}

// 登录并建立授权会话。
jobject login_auth(
    JNIEnv* env,
    jobject /* thiz */,
    jstring license,
    jclass result_class
) {
    clear_mod_payload_seed();
    const std::string license_value = nyx::app_bridge::string_from(env, license);
    const auto result = nyx::sdk::auth::Login(license_value.c_str());
    if (!result.success) {
        clear_mod_payload_seed();
    }
    record("login", result);
    return auth_result_object(env, result_class, "login", result, {});
}

// 登出并清理本地授权会话。
jobject logout_auth(JNIEnv* env, jobject /* thiz */, jclass result_class) {
    clear_mod_payload_seed();
    nyx::sdk::auth::Logout();
    const auto result = ok("logged out");
    record("logout", result);
    return auth_result_object(env, result_class, "logout", result, {});
}

// 检查指定授权功能是否允许运行。
jobject can_run_auth(
    JNIEnv* env,
    jobject /* thiz */,
    jstring feature,
    jclass result_class
) {
    const std::string feature_value = nyx::app_bridge::string_from(env, feature);
    nyx::sdk::auth::Result result;
    if (feature_value.empty()) {
        result = fail(-3002, nyx::sdk::auth::Err::Rejected, "auth feature is empty");
    } else if (!nyx::sdk::auth::CanRun(feature_value.c_str())) {
        result = fail(-3003, nyx::sdk::auth::Err::Rejected, "runtime feature denied");
    } else if (NyxModCanRunAuthRuntime != nullptr &&
               !NyxModCanRunAuthRuntime(feature_value.c_str())) {
        result = fail(-3004, nyx::sdk::auth::Err::Runtime, "active mod runtime gate denied");
    } else {
        result = ok("runtime allowed");
    }
    record("canRun", result, feature_value);
    return auth_result_object(env, result_class, "canRun", result, feature_value);
}

// 检查 runtime 总入口是否允许运行。
jobject can_run_runtime(
    JNIEnv* env,
    jobject /* thiz */,
    jclass result_class
) {
    nyx::sdk::auth::Result result;
    if (!configured()) {
        result = fail(-3002, nyx::sdk::auth::Err::LocalState, "auth runtime is not configured");
    } else if (!nyx::sdk::auth::IsLoggedIn()) {
        result = fail(-3003, nyx::sdk::auth::Err::Rejected, "auth session missing");
    } else if (NyxModCanRunAuthRuntime != nullptr &&
               !NyxModCanRunAuthRuntime("runtime")) {
        result = fail(-3004, nyx::sdk::auth::Err::Runtime, "active mod runtime gate denied");
    } else {
        result = ok("runtime allowed");
    }
    record("canRunRuntime", result);
    return auth_result_object(env, result_class, "canRunRuntime", result, {});
}

// 获取远程授权变量。
jobject fetch_auth_var(
    JNIEnv* env,
    jobject /* thiz */,
    jstring key,
    jclass result_class
) {
    const std::string key_value = nyx::app_bridge::string_from(env, key);
    const auto value = nyx::sdk::auth::TryGetVar(key_value.c_str());
    const std::string text = value.ok() ? value.value : std::string();
    const auto result = value.result;
    record("fetch", result, text);
    return auth_result_object(env, result_class, "fetch", result, text);
}

// 获取远程授权公告。
jobject fetch_auth_notice(JNIEnv* env, jobject /* thiz */, jclass result_class) {
    const auto value = nyx::sdk::auth::TryGetNotice();
    const std::string text = value.ok() ? value.value : std::string();
    const auto result = value.result;
    record("notice", result, text);
    return auth_result_object(env, result_class, "notice", result, text);
}

// 获取远程授权更新信息。
jobject fetch_auth_update(JNIEnv* env, jobject /* thiz */, jclass result_class) {
    const auto value = nyx::sdk::auth::TryCheckUpdate();
    const std::string text = value.ok() ? value.value : std::string();
    const auto result = value.result;
    record("update", result, text);
    return auth_result_object(env, result_class, "update", result, text);
}

// 从授权会话安装模块 payload seed。
jobject install_payload_seed_from_auth(JNIEnv* env, jobject /* thiz */, jclass result_class) {
    const auto result = NyxModInstallPayloadSeedFromAuth == nullptr
        ? fail(-3005, nyx::sdk::auth::Err::LocalState, "active mod has no payload seed installer")
        : (NyxModInstallPayloadSeedFromAuth()
            ? ok("payload seed installed")
            : fail(-3006, nyx::sdk::auth::Err::Runtime, "payload seed install failed"));
    record("installPayloadSeed", result);
    return auth_result_object(env, result_class, "installPayloadSeed", result, {});
}

// Kotlin NativeBridge 的授权 JNI 表。
const JNINativeMethod k_auth_methods[] = {
    {
        "nativeConfigureAuth",
        "(Landroid/content/Context;Ljava/lang/Class;)Ljava/lang/Object;",
        reinterpret_cast<void*>(configure_auth),
    },
    {"nativeAuthStatus", "(Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(auth_status)},
    {"nativeLoginAuth", "(Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(login_auth)},
    {"nativeLogoutAuth", "(Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(logout_auth)},
    {"nativeCanRunAuth", "(Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(can_run_auth)},
    {"nativeCanRunRuntime", "(Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(can_run_runtime)},
    {"nativeFetchAuthVar", "(Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(fetch_auth_var)},
    {"nativeFetchAuthNotice", "(Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(fetch_auth_notice)},
    {"nativeFetchAuthUpdate", "(Ljava/lang/Class;)Ljava/lang/Object;", reinterpret_cast<void*>(fetch_auth_update)},
    {
        "nativeInstallPayloadSeedFromAuth",
        "(Ljava/lang/Class;)Ljava/lang/Object;",
        reinterpret_cast<void*>(install_payload_seed_from_auth),
    },
};

} // namespace

namespace nyx {
namespace app_bridge {

// 注册授权相关 JNI 方法。
bool register_auth(JNIEnv* env) {
    return register_methods(
        env,
        k_native_bridge_class,
        k_auth_methods,
        static_cast<int>(sizeof(k_auth_methods) / sizeof(k_auth_methods[0]))
    );
}

} // namespace app_bridge
} // namespace nyx
