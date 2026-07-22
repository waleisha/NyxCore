#include "src/core/auth/auth_facade.h"

#include "src/core/auth/auth_client.h"
#include "src/core/auth/auth_context.h"
#include "sdk/include/crypto.h"
#include "src/utils/string/hex.h"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace nyx {
namespace sdk {
namespace auth {

namespace {

// 初始化错误码
constexpr int kInitCode = -3001;
// 便捷取值缓冲区错误码
constexpr int kValueBufferCode = -3002;

// 构造失败结果
Result fail(int code, Err failure, std::string message) {
    Result result;
    result.code = code;
    result.failure = failure;
    result.message = std::move(message);
    return result;
}

// 判断失败是否来自输出缓冲区过小
bool output_too_small(const Result& result) {
    return !result.success &&
        result.failure == Err::LocalState &&
        result.message.find("output buffer") != std::string::npos;
}

// 便捷取值接口会逐级扩大缓冲区，避免要求调用方手动传 buffer
template <class Fetch>
Value<std::string> fetch_value(Fetch&& fetch) {
    constexpr std::size_t kSizes[] = {4096, 16384, 65536};
    Value<std::string> out;

    for (const std::size_t size : kSizes) {
        std::vector<char> buffer(size);
        out.result = fetch(buffer.data(), buffer.size());
        if (out.result.success) {
            out.value = buffer.data();
            return out;
        }
        if (!output_too_small(out.result)) {
            return out;
        }
    }

    out.result = fail(kValueBufferCode, Err::LocalState, "auth value is too large");
    out.value.clear();
    return out;
}

// 便捷字符串接口失败时记录日志并返回空字符串
std::string direct_value(const char* operation, const Value<std::string>& value) {
    if (!value.ok()) {
        NYX_LOGW(
            "auth %s failed: code=%d failure=%d %s",
            operation,
            value.result.code,
            static_cast<int>(value.result.failure),
            value.result.message.c_str()
        );
        return {};
    }
    return value.value;
}

// 测试入口直接使用传入的 C++ 上下文
Result configure_for_test(const Context& context, const InitConfig& config) {
    return core::auth::client::configure(context, config);
}

#if defined(__ANDROID__)
// JNI 局部引用 RAII 包装
class LocalRef {
public:
    LocalRef() = default;

    LocalRef(JNIEnv* env, jobject ref)
        : env_(env), ref_(ref) {}

    LocalRef(const LocalRef&) = delete;
    LocalRef& operator=(const LocalRef&) = delete;

    LocalRef(LocalRef&& other) noexcept
        : env_(other.env_), ref_(other.ref_) {
        other.env_ = nullptr;
        other.ref_ = nullptr;
    }

    LocalRef& operator=(LocalRef&& other) noexcept {
        if (this != &other) {
            reset();
            env_ = other.env_;
            ref_ = other.ref_;
            other.env_ = nullptr;
            other.ref_ = nullptr;
        }
        return *this;
    }

    ~LocalRef() {
        reset();
    }

    jobject get() const {
        return ref_;
    }

    explicit operator bool() const {
        return ref_ != nullptr;
    }

private:
    void reset() {
        if (env_ != nullptr && ref_ != nullptr) {
            env_->DeleteLocalRef(ref_);
        }
        ref_ = nullptr;
    }

    JNIEnv* env_ = nullptr;
    jobject ref_ = nullptr;
};

// 清除 JNI 异常，反射读取失败时返回 false/空值
bool clear_exception(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

// 获取对象类引用
LocalRef object_class(JNIEnv* env, jobject object) {
    if (env == nullptr || object == nullptr) {
        return {};
    }
    return LocalRef(env, env->GetObjectClass(object));
}

// 查找实例方法，失败时清理 JNI 异常
jmethodID method_id(JNIEnv* env, jobject object, const char* name, const char* signature) {
    LocalRef klass = object_class(env, object);
    if (!klass) {
        return nullptr;
    }
    auto* id = env->GetMethodID(static_cast<jclass>(klass.get()), name, signature);
    if (clear_exception(env)) {
        return nullptr;
    }
    return id;
}

// 查找实例字段，失败时清理 JNI 异常
jfieldID field_id(JNIEnv* env, jobject object, const char* name, const char* signature) {
    LocalRef klass = object_class(env, object);
    if (!klass) {
        return nullptr;
    }
    auto* id = env->GetFieldID(static_cast<jclass>(klass.get()), name, signature);
    if (clear_exception(env)) {
        return nullptr;
    }
    return id;
}

// 调用返回对象的方法
LocalRef call_object(JNIEnv* env, jobject object, const char* name, const char* signature) {
    auto* id = method_id(env, object, name, signature);
    if (id == nullptr) {
        return {};
    }
    jobject value = env->CallObjectMethod(object, id);
    if (clear_exception(env)) {
        return {};
    }
    return LocalRef(env, value);
}

// 调用返回 bool 的方法
bool call_bool(JNIEnv* env, jobject object, const char* name, const char* signature) {
    auto* id = method_id(env, object, name, signature);
    if (id == nullptr) {
        return false;
    }
    const bool value = env->CallBooleanMethod(object, id) == JNI_TRUE;
    if (clear_exception(env)) {
        return false;
    }
    return value;
}

// 将 jstring 复制成 std::string
std::string string_from(JNIEnv* env, jstring value) {
    if (env == nullptr || value == nullptr) {
        return {};
    }

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        clear_exception(env);
        return {};
    }

    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

// 调用返回字符串的方法
std::string call_string(JNIEnv* env, jobject object, const char* name, const char* signature) {
    LocalRef value = call_object(env, object, name, signature);
    return string_from(env, static_cast<jstring>(value.get()));
}

// 读取字符串字段
std::string string_field(JNIEnv* env, jobject object, const char* name) {
    auto* id = field_id(env, object, name, "Ljava/lang/String;");
    if (id == nullptr) {
        return {};
    }
    LocalRef value(env, env->GetObjectField(object, id));
    if (clear_exception(env)) {
        return {};
    }
    return string_from(env, static_cast<jstring>(value.get()));
}

// 获取应用私有文件目录
std::string files_dir(JNIEnv* env, jobject context) {
    LocalRef dir = call_object(env, context, "getFilesDir", "()Ljava/io/File;");
    return call_string(env, dir.get(), "getAbsolutePath", "()Ljava/lang/String;");
}

// 获取应用包名
std::string package_name(JNIEnv* env, jobject context) {
    return call_string(env, context, "getPackageName", "()Ljava/lang/String;");
}

// 获取 APK 路径
std::string source_dir(JNIEnv* env, jobject context) {
    LocalRef info = call_object(
        env,
        context,
        "getApplicationInfo",
        "()Landroid/content/pm/ApplicationInfo;"
    );
    return string_field(env, info.get(), "sourceDir");
}

// 读取 Android Settings.Secure.ANDROID_ID
std::string android_id(JNIEnv* env, jobject context) {
    LocalRef resolver = call_object(
        env,
        context,
        "getContentResolver",
        "()Landroid/content/ContentResolver;"
    );
    if (!resolver) {
        return {};
    }

    LocalRef secure_class(env, env->FindClass("android/provider/Settings$Secure"));
    if (!secure_class || clear_exception(env)) {
        return {};
    }
    auto* get_string = env->GetStaticMethodID(
        static_cast<jclass>(secure_class.get()),
        "getString",
        "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;"
    );
    if (get_string == nullptr || clear_exception(env)) {
        return {};
    }

    LocalRef key(env, env->NewStringUTF("android_id"));
    LocalRef value(env, env->CallStaticObjectMethod(
        static_cast<jclass>(secure_class.get()),
        get_string,
        resolver.get(),
        key.get()
    ));
    if (clear_exception(env)) {
        return {};
    }
    return string_from(env, static_cast<jstring>(value.get()));
}

// 获取 Android SDK 版本
int sdk_int(JNIEnv* env) {
    LocalRef version_class(env, env->FindClass("android/os/Build$VERSION"));
    if (!version_class || clear_exception(env)) {
        return 0;
    }
    auto* field = env->GetStaticFieldID(static_cast<jclass>(version_class.get()), "SDK_INT", "I");
    if (field == nullptr || clear_exception(env)) {
        return 0;
    }
    return env->GetStaticIntField(static_cast<jclass>(version_class.get()), field);
}

// 读取当前包信息
LocalRef package_info(JNIEnv* env, jobject context, const std::string& package, jint flags) {
    LocalRef manager = call_object(
        env,
        context,
        "getPackageManager",
        "()Landroid/content/pm/PackageManager;"
    );
    if (!manager || package.empty()) {
        return {};
    }

    auto* get_info = method_id(
        env,
        manager.get(),
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;"
    );
    if (get_info == nullptr) {
        return {};
    }

    LocalRef name(env, env->NewStringUTF(package.c_str()));
    LocalRef info(env, env->CallObjectMethod(manager.get(), get_info, name.get(), flags));
    if (clear_exception(env)) {
        return {};
    }
    return info;
}

// 读取对象字段引用
LocalRef object_field(JNIEnv* env, jobject object, const char* name, const char* signature) {
    auto* id = field_id(env, object, name, signature);
    if (id == nullptr) {
        return {};
    }
    LocalRef value(env, env->GetObjectField(object, id));
    if (clear_exception(env)) {
        return {};
    }
    return value;
}

// 获取应用签名列表，兼容 Android 9 前后的签名 API
LocalRef signatures(JNIEnv* env, jobject context, const std::string& package) {
    constexpr jint kGetSignatures = 0x00000040;
    constexpr jint kGetSigningCertificates = 0x08000000;

    if (sdk_int(env) >= 28) {
        LocalRef info = package_info(env, context, package, kGetSigningCertificates);
        LocalRef signing = object_field(
            env,
            info.get(),
            "signingInfo",
            "Landroid/content/pm/SigningInfo;"
        );
        if (!signing) {
            return {};
        }

        const bool multiple = call_bool(env, signing.get(), "hasMultipleSigners", "()Z");
        return call_object(
            env,
            signing.get(),
            multiple ? "getApkContentsSigners" : "getSigningCertificateHistory",
            "()[Landroid/content/pm/Signature;"
        );
    }

    LocalRef info = package_info(env, context, package, kGetSignatures);
    return object_field(env, info.get(), "signatures", "[Landroid/content/pm/Signature;");
}

// 将 Signature 对象转换为原始证书字节
std::vector<std::uint8_t> signature_bytes(JNIEnv* env, jobject signature) {
    LocalRef bytes_ref = call_object(env, signature, "toByteArray", "()[B");
    auto* bytes = static_cast<jbyteArray>(bytes_ref.get());
    if (bytes == nullptr) {
        return {};
    }

    const jsize size = env->GetArrayLength(bytes);
    if (size <= 0 || clear_exception(env)) {
        return {};
    }

    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(out.data()));
    if (clear_exception(env)) {
        return {};
    }
    return out;
}

// 计算首个应用签名证书的 SHA-256
std::string cert_sha256(JNIEnv* env, jobject context, const std::string& package) {
    LocalRef array_ref = signatures(env, context, package);
    auto* array = static_cast<jobjectArray>(array_ref.get());
    if (array == nullptr) {
        return {};
    }

    const jsize count = env->GetArrayLength(array);
    if (count <= 0 || clear_exception(env)) {
        return {};
    }

    LocalRef signature(env, env->GetObjectArrayElement(array, 0));
    if (clear_exception(env)) {
        return {};
    }

    const std::vector<std::uint8_t> bytes = signature_bytes(env, signature.get());
    if (bytes.empty()) {
        return {};
    }

    std::array<std::uint8_t, 32> digest{};
    crypt::Sha256Raw(bytes.data(), bytes.size(), digest.data());
    return ::nyx::utils::string::hex(digest.data(), digest.size());
}
#endif

} // namespace

#if defined(__ANDROID__)
// Android 初始化入口：从 Context 中提取鉴权绑定信息
Result Init(JNIEnv* env, jobject context, const InitConfig& config) {
    if (env == nullptr || context == nullptr) {
        return fail(kInitCode, Err::LocalState, "missing Android auth context");
    }

    LocalRef app = call_object(
        env,
        context,
        "getApplicationContext",
        "()Landroid/content/Context;"
    );
    jobject app_context = app ? app.get() : context;

    const std::string files = files_dir(env, app_context);
    const std::string package = package_name(env, app_context);
    const std::string source = source_dir(env, app_context);
    const std::string android = android_id(env, app_context);
    const std::string cert = cert_sha256(env, app_context, package);

    Context auth_context;
    auth_context.files_dir = files.c_str();
    auth_context.android_id = android.c_str();
    auth_context.package_name = package.c_str();
    auth_context.source_dir = source.c_str();
    auth_context.cert_sha256 = cert.c_str();
    return InitForTest(auth_context, config);
}
#endif

// 测试初始化入口：直接使用调用方提供的上下文
Result InitForTest(const Context& context, const InitConfig& config) {
    return configure_for_test(context, config);
}

// 登录并建立会话
Result Login(const char* license) {
    return core::auth::client::login(license);
}

// 登出并清理本地会话
void Logout() {
    core::auth::client::logout();
}

// 判断当前是否有有效会话
bool IsLoggedIn() {
    return core::auth::client::has_session();
}

// 判断当前会话是否授权指定功能
bool HasFeature(const char* feature) {
    return core::auth::client::is_feature_licensed(core::auth::copy_value(feature));
}

// 判断指定功能是否允许运行
bool CanRun(const char* feature) {
    return HasFeature(feature);
}

// 导出短生命周期能力票据
Value<Capability> ExportCapability(CapabilityPurpose purpose) {
    return core::auth::client::export_capability(purpose);
}

// 校验短生命周期能力票据
bool VerifyCapability(CapabilityPurpose purpose, const Capability& capability) {
    return core::auth::client::verify_capability(purpose, capability);
}

// 获取远程变量到调用方缓冲区
Result GetVar(const char* key, char* out, std::size_t out_len) {
    return core::auth::client::fetch_var(key, out, out_len);
}

// 获取远程变量，自动管理缓冲区
Value<std::string> TryGetVar(const char* key) {
    return fetch_value([key](char* out, std::size_t out_len) {
        return GetVar(key, out, out_len);
    });
}

// 获取远程变量，失败时返回空字符串
std::string GetVar(const char* key) {
    return direct_value("GetVar", TryGetVar(key));
}

// 获取公告到调用方缓冲区
Result GetNotice(char* out, std::size_t out_len) {
    return core::auth::client::fetch_notice(out, out_len);
}

// 获取公告，自动管理缓冲区
Value<std::string> TryGetNotice() {
    return fetch_value([](char* out, std::size_t out_len) {
        return GetNotice(out, out_len);
    });
}

// 获取公告，失败时返回空字符串
std::string GetNotice() {
    return direct_value("GetNotice", TryGetNotice());
}

// 检查更新到调用方缓冲区
Result CheckUpdate(char* out, std::size_t out_len) {
    return core::auth::client::fetch_update(out, out_len);
}

// 检查更新，自动管理缓冲区
Value<std::string> TryCheckUpdate() {
    return fetch_value([](char* out, std::size_t out_len) {
        return CheckUpdate(out, out_len);
    });
}

// 检查更新，失败时返回空字符串
std::string CheckUpdate() {
    return direct_value("CheckUpdate", TryCheckUpdate());
}

} // namespace auth
} // namespace sdk
} // namespace nyx

namespace nyx {
namespace core {
namespace auth {
namespace doctor {

// 保存当前鉴权状态
Snapshot Save() {
    Snapshot snapshot;
    snapshot.state = client::save_snapshot().state;
    return snapshot;
}

// 恢复鉴权状态
void Restore(const Snapshot& snapshot) {
    client::Snapshot copy;
    copy.state = snapshot.state;
    client::restore_snapshot(copy);
}

// 重置鉴权状态
void Reset() {
    client::reset();
}

// 切换到无 provider 模式
void UseNoProvider() {
    client::use_no_provider();
}

// 切换到 mock provider
void UseMock() {
    client::use_mock();
}

// 设置 mock 心跳失败次数
void SetHeartbeatFailures(int count) {
    client::set_heartbeat_failures(count);
}

// 触发一次心跳
sdk::auth::Result Heartbeat() {
    return client::heartbeat();
}

// 设置当前线程的渲染线程标记
void SetRenderThread(bool enabled) {
    client::set_render_thread(enabled);
}

// 获取当前设备 ID
std::string DeviceId() {
    return client::device_id();
}

// 判断本地会话封存文件是否存在
bool IsLoggedInStore() {
    return client::has_session_store();
}

// 获取本地会话封存文件路径
std::string SessionStorePath() {
    return client::session_store_path();
}

} // namespace doctor
} // namespace auth
} // namespace core
} // namespace nyx
