#pragma once

#include <cstddef>
#include <string>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace net {

// HTTP 响应：status 为 HTTP 状态码，error_code 为本地错误码
struct HttpResponse {
    // HTTP 状态码
    int status = -1;
    // 响应体
    std::string body;
    // 本地错误码
    int error_code = 0;
};

// 自定义 CA 证书包
struct CaBundle {
    // PEM 内容
    const char* pem = nullptr;
    // PEM 字节数
    std::size_t size = 0;
};

// URL 无效
inline constexpr int kInvalidUrl = -1001;
// 禁止非安全 URL
inline constexpr int kInsecureUrl = -1002;
// 禁止在主线程发起网络请求
inline constexpr int kMainThreadRequest = -1003;
// HTTP 客户端不可用
inline constexpr int kInvalidClient = -1004;
// 响应体超过限制
inline constexpr int kBodyTooLarge = -1005;

// 拉取配置内容
NYX_EXPORT HttpResponse GetConfig(
    const std::string& url,
    const std::string& endpoint,
    int conn_timeout = 3,
    int read_timeout = 5,
    CaBundle ca = {}
);

// 发送 POST 请求
NYX_EXPORT HttpResponse Post(
    const std::string& url,
    const std::string& endpoint,
    const std::string& body,
    int conn_timeout = 3,
    int read_timeout = 5,
    std::size_t max_body_len = 65536,
    CaBundle ca = {}
);

} // namespace net
} // namespace sdk
} // namespace nyx
