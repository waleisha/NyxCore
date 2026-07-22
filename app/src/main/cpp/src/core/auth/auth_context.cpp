#include "src/core/auth/auth_context.h"

#include "sdk/include/crypto.h"

namespace nyx {
namespace core {
namespace auth {

// 从 SDK 上下文复制一份内部状态
ContextState ContextState::from(const sdk::auth::Context& context) {
    ContextState out;
    out.configured = true;
    out.files_dir = copy_value(context.files_dir);
    out.android_id = copy_value(context.android_id);
    out.package_name = copy_value(context.package_name);
    out.source_dir = copy_value(context.source_dir);
    out.cert_sha256 = copy_value(context.cert_sha256);
    return out;
}

// 判断上下文是否满足鉴权运行要求
bool ContextState::is_valid() const {
    return configured && !files_dir.empty() && !package_name.empty();
}

// 生成用于绑定设备和会话存储的上下文指纹
std::string ContextState::fingerprint() const {
    return ::nyx::sdk::crypt::Sha256(
        "nyx-auth-context-v1\n" +
        files_dir + "\n" +
        package_name + "\n" +
        source_dir + "\n" +
        cert_sha256 + "\n" +
        android_id
    );
}

// 获取鉴权本地存储目录
std::string ContextState::store_dir() const {
    return files_dir + "/nyx_auth";
}

// 比较上下文是否完全一致
bool ContextState::operator==(const ContextState& other) const {
    return configured == other.configured &&
        files_dir == other.files_dir &&
        android_id == other.android_id &&
        package_name == other.package_name &&
        source_dir == other.source_dir &&
        cert_sha256 == other.cert_sha256;
}

bool ContextState::operator!=(const ContextState& other) const {
    return !(*this == other);
}

// 安全复制 C 字符串，空指针转为空字符串
std::string copy_value(const char* value) {
    return value != nullptr ? std::string(value) : std::string();
}

} // namespace auth
} // namespace core
} // namespace nyx
