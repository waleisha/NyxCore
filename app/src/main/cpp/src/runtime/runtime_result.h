#pragma once

#include <string>

namespace nyx {
namespace runtime {

// runtime 通用状态码
enum class RuntimeStatus {
    // 成功
    Ok,
    // 功能路径被禁用
    Disabled,
    // 当前环境不可用
    Unavailable,
    // 目标不存在
    NotFound,
    // 参数无效
    InvalidArgument,
    // 权限或策略拒绝
    Denied,
    // 操作失败
    Failed,
};

// runtime 通用结果
struct RuntimeResult {
    // 状态码
    RuntimeStatus status = RuntimeStatus::Ok;
    // 结果详情
    std::string detail;

    // 是否成功
    bool ok() const {
        return status == RuntimeStatus::Ok;
    }
};

// 构造禁用状态结果
inline RuntimeResult disabled_result(const char* detail) {
    return RuntimeResult{RuntimeStatus::Disabled, detail != nullptr ? detail : "runtime path disabled"};
}

// 获取状态码文本名
inline const char* status_name(RuntimeStatus status) {
    switch (status) {
        case RuntimeStatus::Ok:
            return "ok";
        case RuntimeStatus::Disabled:
            return "disabled";
        case RuntimeStatus::Unavailable:
            return "unavailable";
        case RuntimeStatus::NotFound:
            return "not_found";
        case RuntimeStatus::InvalidArgument:
            return "invalid_argument";
        case RuntimeStatus::Denied:
            return "denied";
        case RuntimeStatus::Failed:
            return "failed";
    }

    return "unknown";
}

} // namespace runtime
} // namespace nyx
