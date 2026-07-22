#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sdk/include/auth.h"

namespace nyx {
namespace core {
namespace auth {

// 构造 SDK 鉴权结果
inline sdk::auth::Result make_result(
    bool success,
    int code,
    sdk::auth::Err failure,
    std::string message
) {
    sdk::auth::Result result;
    result.success = success;
    result.code = code;
    result.failure = failure;
    result.message = std::move(message);
    return result;
}

// 成功结果
inline sdk::auth::Result ok(std::string message = "ok") {
    return make_result(true, 0, sdk::auth::Err::None, std::move(message));
}

// 失败结果
inline sdk::auth::Result fail(
    int code,
    sdk::auth::Err failure,
    std::string message
) {
    return make_result(false, code, failure, std::move(message));
}

// 登录请求输入
struct LoginInput {
    // 用户授权码
    std::string license;
    // 当前设备 ID
    std::string device_id;
};

// 会话请求输入
struct SessionInput {
    // 用户授权码
    std::string license;
    // 当前设备 ID
    std::string device_id;
    // 服务端会话令牌
    std::string token;
};

// 远程变量请求输入
struct VarInput {
    // 用户授权码
    std::string license;
    // 当前设备 ID
    std::string device_id;
    // 变量键名
    std::string key;
    // 服务端会话令牌
    std::string token;
};

// 功能授权票据
struct FeatureTicket {
    // 功能标识
    std::string feature;
    // 是否允许使用该功能
    bool allowed = false;
    // 过期时间戳，0 表示不过期
    std::int64_t expires_at = 0;
};

// Provider 返回结果：统一不同鉴权服务的响应格式
struct ProviderResult {
    // 请求是否成功
    bool success = false;
    // 服务端或本地错误码
    int code = 0;
    // 失败分类
    sdk::auth::Err failure = sdk::auth::Err::None;
    // 可读错误或提示信息
    std::string message;
    // 会话令牌
    std::string token;
    // 服务端校验字段
    std::string check;
    // 是否包含校验字段
    bool has_check = false;
    // 会话过期时间戳，0 表示不过期
    std::int64_t expires_at = 0;
    // 剩余使用次数
    int remaining_uses = 0;
    // 功能授权列表
    std::vector<FeatureTicket> features;
    // 远程变量或公告等载荷
    std::string value;
    // 原始响应文本
    std::string raw;
    // 服务端时间戳
    std::int64_t server_time = 0;
};

// 鉴权服务适配接口
class IProvider {
public:
    virtual ~IProvider() = default;

    // 登录并创建服务端会话
    virtual ProviderResult Login(const LoginInput& input) = 0;
    // 刷新当前会话
    virtual ProviderResult Heartbeat(const SessionInput& input) = 0;
    // 获取远程变量
    virtual ProviderResult GetVar(const VarInput& input) = 0;

    // 获取公告，默认表示当前 provider 不支持
    virtual ProviderResult GetNotice() {
        ProviderResult result;
        result.failure = sdk::auth::Err::Protocol;
        result.message = "provider notice is not supported";
        return result;
    }

    // 检查更新，默认表示当前 provider 不支持
    virtual ProviderResult CheckUpdate() {
        ProviderResult result;
        result.failure = sdk::auth::Err::Protocol;
        result.message = "provider update is not supported";
        return result;
    }
};

} // namespace auth
} // namespace core
} // namespace nyx
