#pragma once

#include "src/core/auth/auth_context.h"

#include <string>

namespace nyx {
namespace core {
namespace auth {

// 设备 ID 获取结果
struct DeviceIdResult {
    // 是否成功获取或创建设备 ID
    bool success = false;
    // 是否本次新生成
    bool fresh = false;
    // 设备 ID
    std::string id;
    // 失败原因
    std::string message;
};

// 获取或创建绑定当前上下文的设备 ID
DeviceIdResult ensure_device_id(const ContextState& context);
// 清除本地设备 ID 封存文件
void clear_device_id(const ContextState& context);

} // namespace auth
} // namespace core
} // namespace nyx
