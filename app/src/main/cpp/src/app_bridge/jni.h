#pragma once

#include <jni.h>

#include <string>

namespace nyx {
namespace app_bridge {

// 按类名注册 JNI 方法，optional 为真时允许目标类不存在。
bool register_methods(
    JNIEnv* env,
    const char* class_name,
    const JNINativeMethod* methods,
    int count,
    bool optional = false
);

// 注册授权桥接 JNI 方法。
bool register_auth(JNIEnv* env);

// 注册渲染和输入桥接 JNI 方法。
bool register_surface(JNIEnv* env);

// 注册 native 集成测试 JNI 方法。
bool register_tests(JNIEnv* env);

// 将 Java 字符串复制为 std::string。
inline std::string string_from(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) {
        return {};
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        return {};
    }

    std::string text(chars);
    env->ReleaseStringUTFChars(value, chars);
    return text;
}

// 将 std::string 转为 Java 字符串。
inline jstring string_to_jni(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

} // namespace app_bridge
} // namespace nyx
