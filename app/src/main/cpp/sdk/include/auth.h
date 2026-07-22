#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sdk/include/utils.h"

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace nyx {
namespace sdk {
namespace auth {

// 授权失败类型
enum class Err {
    None,
    Network,
    Protocol,
    Rejected,
    LocalState,
    Runtime,
};

// 授权上下文：Android 初始化后会从系统 Context 中提取这些字段
struct Context {
    // 应用文件目录
    const char* files_dir = nullptr;
    // Android ID
    const char* android_id = nullptr;
    // 包名
    const char* package_name = nullptr;
    // APK 路径
    const char* source_dir = nullptr;
    // 签名证书 SHA-256
    const char* cert_sha256 = nullptr;
};

// 授权返回值
struct Result {
    // 是否成功
    bool success = false;
    // 业务返回码
    int code = 0;
    // 失败类型
    Err failure = Err::None;
    // 返回消息
    std::string message;
};

// 微验授权配置
struct WyProfile {
    // 服务域名
    std::string host;
    // API 路径前缀
    std::string path_prefix;
    // API token
    std::string api_token;
    // 应用 key
    std::string app_key;

    // 登录接口 call id
    std::string login_call_id;
    // 登录成功码
    int login_success_code = 0;
    // 心跳接口 call id
    std::string heartbeat_call_id;
    // 心跳成功码
    int heartbeat_success_code = 0;
    // 远程变量接口 call id
    std::string var_call_id;
    // 远程变量成功码
    int var_success_code = 0;
    // 公告接口 call id
    std::string notice_call_id;
    // 公告成功码
    int notice_success_code = 0;
    // 更新检查接口 call id
    std::string update_call_id;
    // 更新检查成功码
    int update_success_code = 0;

    // 登录校验类型
    int login_check_kind = 0;
    // 心跳校验类型
    int heartbeat_check_kind = 2;
    // 远程变量校验类型
    int var_check_kind = 2;
    // 签名盐值
    std::string salt;

    // RC4 密钥
    std::string rc4_key;
    // 自定义编码表
    std::string alphabet;

    // 卡密字段名
    std::string license_field;
    // 设备字段名
    std::string device_field;
    // token 字段名
    std::string token_field;
    // 时间字段名
    std::string time_field;
    // 签名字段名
    std::string sign_field;
    // nonce 字段名
    std::string nonce_field;
    // 远程变量字段名
    std::string var_field;

    // 预置远程变量
    std::vector<std::pair<std::string, std::string>> vars;
};

// 运行时能力票据用途
enum class CapabilityPurpose : std::uint32_t {
    PayloadSeed = 0xA71C0001u,
    PayloadDecrypt = 0xA71C0002u,
    UnityLoad = 0xA71C0003u,
    ImGuiStart = 0xA71C0004u,
    ImGuiRecover = 0xA71C0005u,
};

// 短生命周期能力票据，绑定当前 auth session 和设备上下文
struct Capability {
    std::uint64_t word0 = 0;
    std::uint64_t word1 = 0;
    std::uint64_t word2 = 0;
    std::uint64_t binding0 = 0;
    std::uint64_t binding1 = 0;
    std::int64_t issued_at = 0;
    std::uint32_t purpose = 0;
    std::uint32_t flags = 0;
    std::uint64_t checksum = 0;
};

// 授权初始化配置
struct InitConfig {
    // 微验配置
    WyProfile profile;
};

// 授权模块自己的 Value，按 success 判断是否成功
template <class T>
struct Value {
    // 调用结果
    Result result;
    // 返回数据
    T value;

    // 判断结果是否成功
    bool ok() const {
        return result.success;
    }

    // 允许在 if 中直接判断
    explicit operator bool() const {
        return ok();
    }
};

#if defined(__ANDROID__)
// Android 初始化入口：从 Context 中提取鉴权绑定信息
NYX_EXPORT Result Init(JNIEnv* env, jobject context, const InitConfig& config);
#endif
// 测试初始化入口：直接使用调用方提供的上下文
NYX_EXPORT Result InitForTest(const Context& context, const InitConfig& config);
// 登录并建立会话
NYX_EXPORT Result Login(const char* license);
// 登出并清理本地会话
NYX_EXPORT void Logout();
// 判断当前是否有有效会话
NYX_EXPORT bool IsLoggedIn();
// 判断当前会话是否授权指定功能
NYX_EXPORT bool HasFeature(const char* feature);
// 判断指定功能是否允许运行
NYX_EXPORT bool CanRun(const char* feature);
// 导出短生命周期能力票据
NYX_EXPORT Value<Capability> ExportCapability(CapabilityPurpose purpose);
// 校验短生命周期能力票据
NYX_EXPORT bool VerifyCapability(CapabilityPurpose purpose, const Capability& capability);
// 获取远程变量，失败信息保留在 Value 中
NYX_EXPORT Value<std::string> TryGetVar(const char* key);
// 获取远程变量，失败时返回空字符串
NYX_EXPORT std::string GetVar(const char* key);
// 获取公告，失败信息保留在 Value 中
NYX_EXPORT Value<std::string> TryGetNotice();
// 获取公告，失败时返回空字符串
NYX_EXPORT std::string GetNotice();
// 检查更新，失败信息保留在 Value 中
NYX_EXPORT Value<std::string> TryCheckUpdate();
// 检查更新，失败时返回空字符串
NYX_EXPORT std::string CheckUpdate();

} // namespace auth
} // namespace sdk
} // namespace nyx
