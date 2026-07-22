#include "src/runtime/stack/stack_hooks.h"

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/stack/stack_base.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>

namespace nyx {
namespace runtime {
namespace stack {

namespace {

// 没有明确边界时估算的 Hook 代码大小
constexpr std::uintptr_t kEstimatedHookCodeSize = 128;

// 从入口地址估算 Hook 代码范围
AddressRange estimated_range(std::uintptr_t start) {
    start = code_address(start);
    if (start == 0) {
        return {};
    }

    const std::uintptr_t end = start + kEstimatedHookCodeSize;
    if (end <= start) {
        return {};
    }
    return AddressRange{start, end, true};
}

// 清理地址范围中的 ABI 标记位
AddressRange clean_range(AddressRange range) {
    range.start = code_address(range.start);
    range.end = code_address(range.end);
    if (range.end <= range.start) {
        return {};
    }
    return range;
}

// Hook 记录 ID 计数器
std::atomic<std::uint64_t>& next_id() {
    static std::atomic<std::uint64_t> value{1};
    return value;
}

// 判断两条 Hook 记录是否代表同一 owner/target
bool same_record(const HookFrameRecord& lhs, const HookFrameRecord& rhs) {
    return lhs.owner == rhs.owner && lhs.target == rhs.target;
}

// 判断 Hook 记录是否命中 owner/target
bool same_record(const HookFrameRecord& record, const std::string& owner, const std::string& target) {
    return record.owner == owner && record.target == target;
}

// 扫描常见第三方 Hook 框架的可执行段
AddressRange vendor_range() {
    memory::MemoryMap memory_map;
    std::vector<memory::MemoryLibrary> libraries;
    if (!memory_map.libraries(&libraries).ok()) {
        return {};
    }

    for (const auto& library : libraries) {
        const std::string name = lower_copy(library.name + " " + library.path);
        if (name.find("bytehook") == std::string::npos &&
            name.find("shadowhook") == std::string::npos &&
            name.find("dobby") == std::string::npos) {
            continue;
        }

        for (const auto& segment : library.segments) {
            if (segment.executable()) {
                return AddressRange{segment.start, segment.end, true};
            }
        }
    }

    return {};
}

// 第三方 Hook 框架范围缓存
AddressRange& vendor_range_cache() {
    static AddressRange value;
    return value;
}

// 第三方 Hook 框架范围是否已缓存
bool& vendor_range_cached() {
    static bool value = false;
    return value;
}

// 获取缓存后的第三方 Hook 框架范围
AddressRange cached_vendor_range() {
    if (!vendor_range_cached()) {
        vendor_range_cache() = vendor_range();
        vendor_range_cached() = true;
    }
    return vendor_range_cache();
}

// 清理第三方 Hook 框架范围缓存
void clear_vendor_range_cache() {
    vendor_range_cache() = {};
    vendor_range_cached() = false;
}

// 标准帧指针对展开规则
StackUnwindRule frame_pair_rule() {
    StackUnwindRule rule;
    rule.kind = StackUnwindRuleKind::FramePointerPair;
    rule.parent_frame_offset = 0;
    rule.return_address_offset = static_cast<std::intptr_t>(sizeof(std::uintptr_t));
    rule.stack_delta = 0;
    rule.complete = true;
    return rule;
}

// 无帧指针函数的展开规则
StackUnwindRule frameless_rule() {
    StackUnwindRule rule;
    rule.kind = StackUnwindRuleKind::Frameless;
    rule.parent_frame_offset = 0;
    rule.return_address_offset = 0;
    rule.stack_delta = 0;
    rule.complete = true;
    return rule;
}

// 为缺省的 Hook 范围补充展开规则
void fill_unwind_rules(HookFrameRecord* record) {
    if (record == nullptr) {
        return;
    }
    if (record->replacement_range.valid() && !record->replacement_unwind.complete) {
        record->replacement_unwind = frame_pair_rule();
    }
    if (record->trampoline_range.valid() && !record->trampoline_unwind.complete) {
        record->trampoline_unwind = frameless_rule();
    }
    if (record->original_range.valid() && !record->original_unwind.complete) {
        record->original_unwind = frameless_rule();
    }
    if (record->vendor_hub_range.valid() && !record->vendor_hub_unwind.complete) {
        record->vendor_hub_unwind = frameless_rule();
    }
}

// 标准化 Hook 记录并填充缺省值
HookFrameRecord with_defaults(HookFrameRecord record) {
    if (record.id == 0) {
        record.id = next_id().fetch_add(1);
    }
    record.expected_call_site = code_address(record.expected_call_site);
    record.original_entry = code_address(record.original_entry);
    record.replacement_entry = code_address(record.replacement_entry);
    record.target_range = clean_range(record.target_range);
    record.replacement_range = clean_range(record.replacement_range);
    record.trampoline_range = clean_range(record.trampoline_range);
    record.original_range = clean_range(record.original_range);
    record.vendor_hub_range = clean_range(record.vendor_hub_range);
    if (!record.target_range.valid() && record.original_entry != 0) {
        record.target_range = estimated_range(record.original_entry);
    }
    if (!record.replacement_range.valid() && record.replacement_entry != 0) {
        record.replacement_range = estimated_range(record.replacement_entry);
    }
    if (!record.vendor_hub_range.valid()) {
        record.vendor_hub_range = cached_vendor_range();
    }
    fill_unwind_rules(&record);
    return record;
}

} // namespace

// 判断地址是否在范围内
bool AddressRange::contains(std::uintptr_t address) const {
    return valid() && start <= address && address < end;
}

// 判断范围是否有效
bool AddressRange::valid() const {
    return start != 0 && end > start;
}

// 添加或更新 Hook 记录
RuntimeResult StackHookRegistry::add_or_update(const HookFrameRecord& record) {
    if (record.owner.empty() || record.target.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stack hook owner or target"};
    }

    std::lock_guard<std::mutex> lock(mutex());
    auto& items = records_ref();
    auto found = std::find_if(items.begin(), items.end(), [&record](const HookFrameRecord& item) {
        return same_record(item, record);
    });

    HookFrameRecord next = with_defaults(record);
    if (found != items.end()) {
        next.id = found->id;
        *found = std::move(next);
        return RuntimeResult{};
    }

    items.push_back(std::move(next));
    return RuntimeResult{};
}

// 标记 Hook 已移除
RuntimeResult StackHookRegistry::mark_removed(const std::string& owner, const std::string& target) {
    if (owner.empty() || target.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stack hook owner or target"};
    }

    std::lock_guard<std::mutex> lock(mutex());
    auto& items = records_ref();
    auto found = std::find_if(items.begin(), items.end(), [&](const HookFrameRecord& item) {
        return same_record(item, owner, target);
    });
    if (found == items.end()) {
        return RuntimeResult{RuntimeStatus::NotFound, "stack hook record not found"};
    }

    found->installed = false;
    return RuntimeResult{};
}

// 按地址查找已安装的 Hook 记录
RuntimeResult StackHookRegistry::find_by_address(std::uintptr_t address, HookFrameRecord* out) const {
    if (address == 0 || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing address or stack hook output"};
    }
    address = code_address(address);

    std::lock_guard<std::mutex> lock(mutex());
    for (const auto& record : records_ref()) {
        if (!record.installed) {
            continue;
        }
        if (record.target_range.contains(address) ||
            record.replacement_range.contains(address) ||
            record.trampoline_range.contains(address) ||
            record.original_range.contains(address) ||
            record.vendor_hub_range.contains(address)) {
            *out = record;
            return RuntimeResult{};
        }
    }

    *out = HookFrameRecord{};
    return RuntimeResult{RuntimeStatus::NotFound, "address did not match any installed stack hook"};
}

// 获取所有 Hook 记录
std::vector<HookFrameRecord> StackHookRegistry::records() const {
    std::lock_guard<std::mutex> lock(mutex());
    return records_ref();
}

// 清空 Hook 记录和缓存
void StackHookRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex());
    records_ref().clear();
    clear_vendor_range_cache();
}

// 全局注册表锁
std::mutex& StackHookRegistry::mutex() {
    static std::mutex value;
    return value;
}

// 全局注册表存储
std::vector<HookFrameRecord>& StackHookRegistry::records_ref() {
    static std::vector<HookFrameRecord> value;
    return value;
}

} // namespace stack
} // namespace runtime
} // namespace nyx
