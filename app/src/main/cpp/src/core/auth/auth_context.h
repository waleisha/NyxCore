#pragma once

#include "sdk/include/auth.h"

#include <string>

namespace nyx {
namespace core {
namespace auth {

// 鉴权运行上下文：保存本地路径、包名和设备绑定信息
struct ContextState {
    // 是否已经从 SDK 上下文初始化
    bool configured = false;
    // 应用私有文件目录
    std::string files_dir;
    // Android 设备 ID
    std::string android_id;
    // 应用包名
    std::string package_name;
    // APK 路径
    std::string source_dir;
    // 应用签名证书 SHA-256
    std::string cert_sha256;

    // 从 SDK 上下文复制一份内部状态
    static ContextState from(const sdk::auth::Context& context);

    // 判断上下文是否满足鉴权运行要求
    bool is_valid() const;
    // 生成用于绑定设备和会话存储的上下文指纹
    std::string fingerprint() const;
    // 获取鉴权本地存储目录
    std::string store_dir() const;
    // 比较上下文是否完全一致
    bool operator==(const ContextState& other) const;
    bool operator!=(const ContextState& other) const;
};

// 安全复制 C 字符串，空指针转为空字符串
std::string copy_value(const char* value);

} // namespace auth
} // namespace core
} // namespace nyx
