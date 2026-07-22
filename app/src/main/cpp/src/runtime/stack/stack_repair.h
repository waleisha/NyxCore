#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"
#include "src/runtime/stack/stack_frame.h"

namespace nyx {
namespace runtime {
namespace stack {

// 可修复的栈槽类型
enum class StackRepairSlot {
    // 保存的父帧指针
    SavedFramePointer,
    // 保存的返回地址
    SavedReturnAddress,
};

// 单个栈修复补丁
struct StackRepairPatch {
    // 修复槽位
    StackRepairSlot slot = StackRepairSlot::SavedReturnAddress;
    // 栈槽地址
    std::uintptr_t address = 0;
    // 修复前值
    std::uintptr_t before = 0;
    // 修复后值
    std::uintptr_t after = 0;
    // 是否已应用
    bool applied = false;
    // 修复原因
    std::string reason;
};

// 为归一化栈帧规划可安全写入的修复补丁
RuntimeResult plan_stack_repair(
    const std::vector<NormalizedStackFrame>& frames,
    std::vector<StackRepairPatch>* out
);

// 栈修复作用域，析构时自动恢复
class StackRepairScope {
public:
    StackRepairScope() = default;
    StackRepairScope(const StackRepairScope&) = delete;
    StackRepairScope& operator=(const StackRepairScope&) = delete;
    ~StackRepairScope();

    // 应用修复补丁
    RuntimeResult apply(
        const std::vector<NormalizedStackFrame>& frames,
        std::vector<StackRepairPatch>* out = nullptr
    );
    // 恢复已应用补丁
    void restore();

private:
    // 已应用补丁
    std::vector<StackRepairPatch> patches_;
};

} // namespace stack
} // namespace runtime
} // namespace nyx

