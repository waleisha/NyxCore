#pragma once

#include <jni.h>

namespace nyx {
namespace app_bridge {

// 缓存 Application Context 和 AssetManager，供 native runtime 使用。
bool set_android_context(JNIEnv* env, jobject context);

// 释放已缓存的 Android 上下文；env 为空时会临时附加当前线程。
void clear_android_context(JNIEnv* env = nullptr);

} // namespace app_bridge
} // namespace nyx
