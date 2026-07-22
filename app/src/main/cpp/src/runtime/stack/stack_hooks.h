#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace stack {

// Hook 类型
enum class HookType {
    // Inline Hook
    Inline,
    // PLT Hook
    Plt,
};

// 栈展开规则类型
enum class StackUnwindRuleKind {
    // 未知规则
    Unknown,
    // 无帧指针规则
    Frameless,
    // 帧指针保存对规则
    FramePointerPair,
};

// 地址范围
struct AddressRange {
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址，左闭右开
    std::uintptr_t end = 0;
    // 是否为估算范围
    bool estimated = false;

    // 判断地址是否在范围内
    bool contains(std::uintptr_t address) const;
    // 判断范围是否有效
    bool valid() const;
};

// 栈展开规则
struct StackUnwindRule {
    // 规则类型
    StackUnwindRuleKind kind = StackUnwindRuleKind::Unknown;
    // 父帧指针相对偏移
    std::intptr_t parent_frame_offset = 0;
    // 返回地址相对偏移
    std::intptr_t return_address_offset = static_cast<std::intptr_t>(sizeof(std::uintptr_t));
    // 栈指针调整量
    std::intptr_t stack_delta = 0;
    // 规则是否完整
    bool complete = false;
};

// Hook 栈帧记录
struct HookFrameRecord {
    // 记录 ID
    std::uint64_t id = 0;
    // Hook 拥有者
    std::string owner;
    // Hook 目标
    std::string target;
    // Hook 类型
    HookType kind = HookType::Inline;
    // 目标函数范围
    AddressRange target_range;
    // 替换函数范围
    AddressRange replacement_range;
    // 跳板范围
    AddressRange trampoline_range;
    // 原始函数范围
    AddressRange original_range;
    // 第三方 hook hub 范围
    AddressRange vendor_hub_range;
    // 期望恢复到的调用点
    std::uintptr_t expected_call_site = 0;
    // 原始入口地址
    std::uintptr_t original_entry = 0;
    // 替换入口地址
    std::uintptr_t replacement_entry = 0;
    // 替换函数展开规则
    StackUnwindRule replacement_unwind;
    // 跳板展开规则
    StackUnwindRule trampoline_unwind;
    // 原始函数展开规则
    StackUnwindRule original_unwind;
    // 第三方 hub 展开规则
    StackUnwindRule vendor_hub_unwind;
    // Hook 是否已安装
    bool installed = false;
};

// Hook 栈帧注册表
class StackHookRegistry {
public:
    // 添加或更新 Hook 记录
    RuntimeResult add_or_update(const HookFrameRecord& record);
    // 标记 Hook 已移除
    RuntimeResult mark_removed(const std::string& owner, const std::string& target);
    // 按地址查找命中的 Hook 记录
    RuntimeResult find_by_address(std::uintptr_t address, HookFrameRecord* out) const;
    // 获取所有 Hook 记录
    std::vector<HookFrameRecord> records() const;
    // 清空 Hook 记录
    void clear();

private:
    // 全局注册表锁
    static std::mutex& mutex();
    // 全局注册表存储
    static std::vector<HookFrameRecord>& records_ref();
};

} // namespace stack
} // namespace runtime
} // namespace nyx
