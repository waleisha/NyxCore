#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#if defined(_WIN32)
#define NYX_EXPORT
#else
#define NYX_EXPORT __attribute__((visibility("hidden")))
#endif

#ifndef NYX_DEBUG_MODE
#define NYX_DEBUG_MODE 0
#endif

#ifndef NYX_ENABLE_NATIVE_LOGS
#define NYX_ENABLE_NATIVE_LOGS 1
#endif

namespace nyx {
namespace sdk {

// 通用状态码：SDK 接口用它表达失败原因
enum class Status {
    Ok,
    Disabled,
    Unavailable,
    NotFound,
    InvalidArgument,
    Denied,
    Failed,
};

// 通用返回值：status 给机器判断，detail 留给日志和调试
struct Result {
    // 状态码
    Status status = Status::Ok;
    // 详细说明
    std::string detail;

    // 判断调用是否成功
    bool ok() const {
        return status == Status::Ok;
    }
};

// 带返回数据的结果封装
template <class T>
struct Value {
    // 调用结果
    Result result;
    // 返回数据
    T value;

    // 判断结果是否可用
    bool ok() const {
        return result.ok();
    }

    // 允许在 if 中直接判断
    explicit operator bool() const {
        return ok();
    }
};

// 将状态码转成可读字符串
NYX_EXPORT const char* StatusStr(Status status);

namespace utils {

// 日志等级
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

// 写入一条 SDK 日志
NYX_EXPORT void Log(LogLevel level, const char* format, ...);
// 刷新日志缓冲区
NYX_EXPORT void Flush();
// 关闭日志输出
NYX_EXPORT void Close();

// 投递任务到主线程队列
NYX_EXPORT void Post(std::function<void()> task);
// 执行当前队列中的任务，返回执行数量
NYX_EXPORT std::size_t RunTasks();
// 获取等待执行的任务数量
NYX_EXPORT std::size_t TaskCount();

// 判断当前线程是否为主线程
NYX_EXPORT bool IsMain();
// 获取程序已运行时间（微秒）
NYX_EXPORT std::uint64_t Uptime();

} // namespace utils
} // namespace sdk
} // namespace nyx

#if NYX_ENABLE_NATIVE_LOGS
#if NYX_DEBUG_MODE
#define NYX_LOGD(...) ::nyx::sdk::utils::Log(::nyx::sdk::utils::LogLevel::Debug, __VA_ARGS__)
#else
#define NYX_LOGD(...) static_cast<void>(0)
#endif
#define NYX_LOGI(...) ::nyx::sdk::utils::Log(::nyx::sdk::utils::LogLevel::Info, __VA_ARGS__)
#define NYX_LOGW(...) ::nyx::sdk::utils::Log(::nyx::sdk::utils::LogLevel::Warning, __VA_ARGS__)
#define NYX_LOGE(...) ::nyx::sdk::utils::Log(::nyx::sdk::utils::LogLevel::Error, __VA_ARGS__)
#else
#define NYX_LOGD(...) static_cast<void>(0)
#define NYX_LOGI(...) static_cast<void>(0)
#define NYX_LOGW(...) static_cast<void>(0)
#define NYX_LOGE(...) static_cast<void>(0)
#endif
