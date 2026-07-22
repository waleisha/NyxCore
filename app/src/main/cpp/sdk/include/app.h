#pragma once

#include "sdk/include/utils.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <jni.h>
#endif

namespace nyx {
namespace sdk {
namespace app {

#if defined(__ANDROID__)
// 进程级 JavaVM；JNI_OnLoad 后可用，未初始化时返回 nullptr。
NYX_EXPORT JavaVM* GetJavaVM();
// 返回应用 Context 的 local ref；调用方用完后负责 DeleteLocalRef。
NYX_EXPORT jobject GetApplicationContext(JNIEnv* env);
// 返回缓存的 AssetManager 指针；生命周期由 NyxCore app bridge 管理。
NYX_EXPORT AAssetManager* GetAssetManager();
#endif

} // namespace app
} // namespace sdk
} // namespace nyx

