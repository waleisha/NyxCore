#pragma once

#include <cstdint>
#include <string>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/memory/memory_value.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 冻结写入项
struct FreezeItem {
    // 原始文本值
    std::string value;
    // 目标地址
    std::uintptr_t addr = 0;
    // 值类型
    int type = 0;
    // 已解析标量值
    ScalarValue scalar;
};

// 添加冻结项并立即写入一次
RuntimeResult add_freeze(void* owner, const FreezeItem& item, const MemProcess& process, std::uint32_t delay_ms);
// 移除指定冻结项
RuntimeResult remove_freeze(void* owner, std::uintptr_t addr);
// 清除 owner 的全部冻结项
RuntimeResult clear_freeze(void* owner);
// 设置冻结循环间隔
RuntimeResult set_freeze_delay(void* owner, std::uint32_t delay_ms);
// 启动冻结线程
RuntimeResult start_freeze(void* owner, const MemProcess& process, std::uint32_t delay_ms);
// 停止冻结线程
RuntimeResult stop_freeze(void* owner);

} // namespace memory
} // namespace runtime
} // namespace nyx
