#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// WY 服务端入口配置
struct Endpoint {
    // 服务域名，可带协议
    std::string host;
    // API 路径前缀
    std::string path_prefix;
    // API token，拼到路径后面
    std::string api_token;
};

// WY 单个调用配置
struct Call {
    // 调用 ID
    std::string id;
    // 成功响应码
    int success_code = 0;
};

// WY 各业务调用配置
struct Calls {
    // 登录调用
    Call login;
    // 心跳调用
    Call heartbeat;
    // 远程变量调用
    Call var;
    // 公告调用，可选
    std::optional<Call> notice;
    // 更新检查调用，可选
    std::optional<Call> update;
};

// WY 请求字段名配置
struct Fields {
    // 授权码字段名
    std::string license = "kami";
    // 设备字段名
    std::string device = "markcode";
    // 会话令牌字段名
    std::string token = "kamitoken";
    // 时间戳字段名
    std::string time = "t";
    // 签名字段名
    std::string sign = "sign";
    // nonce 字段名
    std::string nonce = "value";
    // 远程变量 key 字段名
    std::string var_key = "key";
};

// 编解码步骤
enum class CodecStep {
    // RC4 加解密
    Rc4,
    // 十六进制编解码
    Hex,
    // 自定义 Base64 编解码
    DefBase,
};

// WY 响应 check 计算方式
enum class CheckKind {
    // time + app_key + nonce + salt，再 MD5 后 SHA-256
    TimeAppKeyNonceSalt = 0,
    // app_key + nonce + salt，再 MD5 后 SHA-256
    AppKeyNonceSalt = 1,
    // time + app_key + nonce，再 MD5
    TimeAppKeyNonce = 2,
};

// WY 编解码配置
struct CodecProfile {
    // RC4 密钥
    std::string rc4_key;
    // 自定义 Base64 字母表
    std::string alphabet;
    // 请求编码步骤
    std::vector<CodecStep> request_steps;
    // 响应解码步骤
    std::vector<CodecStep> response_steps;
};

// WY 响应校验配置
struct CheckProfile {
    // 登录响应校验方式
    CheckKind login = CheckKind::TimeAppKeyNonceSalt;
    // 心跳响应校验方式
    CheckKind heartbeat = CheckKind::TimeAppKeyNonce;
    // 远程变量响应校验方式
    CheckKind var = CheckKind::TimeAppKeyNonce;
    // check 计算盐值
    std::string salt;
    // 允许的服务端时间漂移秒数
    int max_time_skew_seconds = 30;
};

// WY 明文配置
struct Profile {
    // 入口配置
    Endpoint endpoint;
    // 应用密钥
    std::string app_key;
    // 调用配置
    Calls calls;
    // 字段名配置
    Fields fields;
    // 编解码配置
    CodecProfile codec;
    // 响应校验配置
    CheckProfile check;
    // 本地变量别名到远端变量 key 的映射
    std::unordered_map<std::string, std::string> vars;
};

// 运行时字符串：轻量异或存放，减少明文常驻
struct RuntimeText {
    // 异或后的字节
    std::vector<std::uint8_t> bytes;
    // 异或 key
    std::uint8_t key = 0;
};

// 运行时入口配置
struct RuntimeEndpoint {
    // 服务域名
    RuntimeText host;
    // API 路径前缀
    RuntimeText path_prefix;
    // API token
    RuntimeText api_token;
};

// 运行时调用配置
struct RuntimeCall {
    // 调用 ID
    RuntimeText id;
    // 成功响应码
    int success_code = 0;
};

// 运行时调用集合
struct RuntimeCalls {
    // 登录调用
    RuntimeCall login;
    // 心跳调用
    RuntimeCall heartbeat;
    // 远程变量调用
    RuntimeCall var;
    // 公告调用，可选
    std::optional<RuntimeCall> notice;
    // 更新检查调用，可选
    std::optional<RuntimeCall> update;
};

// 运行时字段名配置
struct RuntimeFields {
    // 授权码字段名
    RuntimeText license;
    // 设备字段名
    RuntimeText device;
    // 会话令牌字段名
    RuntimeText token;
    // 时间戳字段名
    RuntimeText time;
    // 签名字段名
    RuntimeText sign;
    // nonce 字段名
    RuntimeText nonce;
    // 远程变量 key 字段名
    RuntimeText var_key;
};

// 运行时编解码配置
struct RuntimeCodecProfile {
    // RC4 密钥
    RuntimeText rc4_key;
    // 自定义 Base64 字母表
    RuntimeText alphabet;
    // 请求编码步骤
    std::vector<CodecStep> request_steps;
    // 响应解码步骤
    std::vector<CodecStep> response_steps;
};

// 运行时响应校验配置
struct RuntimeCheckProfile {
    // 登录响应校验方式
    CheckKind login = CheckKind::TimeAppKeyNonceSalt;
    // 心跳响应校验方式
    CheckKind heartbeat = CheckKind::TimeAppKeyNonce;
    // 远程变量响应校验方式
    CheckKind var = CheckKind::TimeAppKeyNonce;
    // check 计算盐值
    RuntimeText salt;
    // 允许的服务端时间漂移秒数
    int max_time_skew_seconds = 30;
};

// 运行时配置：敏感文本以 RuntimeText 形式保存
struct RuntimeProfile {
    // 入口配置
    RuntimeEndpoint endpoint;
    // 应用密钥
    RuntimeText app_key;
    // 调用配置
    RuntimeCalls calls;
    // 字段名配置
    RuntimeFields fields;
    // 编解码配置
    RuntimeCodecProfile codec;
    // 响应校验配置
    RuntimeCheckProfile check;
    // 本地变量别名到远端变量 key 的映射
    std::vector<std::pair<RuntimeText, RuntimeText>> vars;
};

// 补齐默认 host、字段名、编解码步骤和成功码
Profile with_defaults(Profile profile);
// 判断 profile 是否足够发起 WY 请求
bool is_configured(const Profile& profile);
// 生成请求路径
std::string endpoint_path(const Profile& profile);
// 将本地变量别名解析成 WY 远端变量 key
std::string resolve_var(const Profile& profile, const std::string& key);
// 将明文配置转换为运行时配置
RuntimeProfile store_profile(Profile profile);
// 将运行时配置还原成明文配置
Profile open_profile(const RuntimeProfile& profile);
// 从编译宏或环境变量加载 WY 配置
Profile load_profile();
// 测试用样例配置
Profile sample_profile();

} // namespace wy

} // namespace auth
} // namespace core
} // namespace nyx
