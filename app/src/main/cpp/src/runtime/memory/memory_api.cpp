#include "sdk/include/memory.h"

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_area.h"
#include "src/runtime/memory/memory_freeze.h"
#include "src/runtime/memory/memory_improve.h"
#include "src/runtime/memory/memory_normalizer.h"
#include "src/runtime/memory/memory_process.h"
#include "src/runtime/memory/memory_protect.h"
#include "src/runtime/memory/memory_reader.h"
#include "src/runtime/memory/memory_scan.h"
#include "src/runtime/memory/memory_value.h"
#include "src/runtime/memory/memory_writer.h"
#include "sdk/result_bridge.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <unistd.h>
#include <utility>
#include <vector>

namespace nyx {
namespace sdk {
namespace memory {

namespace detail {

// runtime maps 条目转 SDK 条目
MapEntry to_entry(const runtime::memory::MemoryMapEntry& entry) {
    return MapEntry{
        entry.start,
        entry.end,
        entry.permissions,
        entry.offset,
        entry.device,
        entry.inode,
        entry.path
    };
}

} // namespace detail

namespace {

// SDK maps 条目转 runtime 条目
runtime::memory::MemoryMapEntry to_entry(const MapEntry& entry) {
    return runtime::memory::MemoryMapEntry{
        entry.start,
        entry.end,
        entry.permissions,
        entry.offset,
        entry.device,
        entry.inode,
        entry.path
    };
}

// runtime 库记录转 SDK 库记录
Library to_library(const runtime::memory::MemoryLibrary& library) {
    Library out;
    out.name = library.name;
    out.path = library.path;
    out.start = library.start;
    out.end = library.end;
    out.segments.reserve(library.segments.size());
    for (const auto& segment : library.segments) {
        out.segments.push_back(detail::to_entry(segment));
    }
    return out;
}

// runtime 权限转 SDK 权限
PagePermission to_permission(const runtime::memory::PagePermission& permission) {
    return PagePermission{permission.read, permission.write, permission.execute};
}

// SDK 权限转 runtime 权限
runtime::memory::PagePermission to_permission(const PagePermission& permission) {
    return runtime::memory::PagePermission{permission.read, permission.write, permission.execute};
}

// runtime VMA 差异类型转 SDK 类型
VmaDiffKind to_kind(runtime::memory::VmaDiffKind kind) {
    switch (kind) {
        case runtime::memory::VmaDiffKind::Added:
            return VmaDiffKind::Added;
        case runtime::memory::VmaDiffKind::Removed:
            return VmaDiffKind::Removed;
        case runtime::memory::VmaDiffKind::RangeChanged:
            return VmaDiffKind::RangeChanged;
        case runtime::memory::VmaDiffKind::PermissionChanged:
            return VmaDiffKind::PermissionChanged;
        case runtime::memory::VmaDiffKind::NameChanged:
            return VmaDiffKind::NameChanged;
        case runtime::memory::VmaDiffKind::Unchanged:
            return VmaDiffKind::Unchanged;
    }

    return VmaDiffKind::Unchanged;
}

// runtime VMA 操作类型转 SDK 类型
OperationKind to_kind(runtime::memory::VmaOperationKind kind) {
    switch (kind) {
        case runtime::memory::VmaOperationKind::Protect:
            return OperationKind::Protect;
        case runtime::memory::VmaOperationKind::SetName:
            return OperationKind::SetName;
        case runtime::memory::VmaOperationKind::Advise:
            return OperationKind::Advise;
        case runtime::memory::VmaOperationKind::Resize:
            return OperationKind::Resize;
        case runtime::memory::VmaOperationKind::Remap:
            return OperationKind::Remap;
    }

    return OperationKind::Protect;
}

// runtime VMA 操作状态转 SDK 状态
OperationStatus to_status(runtime::memory::VmaOperationStatus status) {
    switch (status) {
        case runtime::memory::VmaOperationStatus::Planned:
            return OperationStatus::Planned;
        case runtime::memory::VmaOperationStatus::Applied:
            return OperationStatus::Applied;
        case runtime::memory::VmaOperationStatus::RolledBack:
            return OperationStatus::RolledBack;
        case runtime::memory::VmaOperationStatus::Failed:
            return OperationStatus::Failed;
        case runtime::memory::VmaOperationStatus::RollbackFailed:
            return OperationStatus::RollbackFailed;
    }

    return OperationStatus::Failed;
}

// runtime 保护请求转 SDK 保护选项
ProtectOptions to_options(const runtime::memory::PageProtectRequest& request) {
    return ProtectOptions{request.start, request.end, request.read, request.write, request.execute};
}

// SDK 保护选项转 runtime 保护请求
runtime::memory::PageProtectRequest to_request(const ProtectOptions& options) {
    return runtime::memory::PageProtectRequest{
        options.start,
        options.end,
        options.read,
        options.write,
        options.execute
    };
}

// SDK 重映射请求转 runtime 请求
runtime::memory::RemapRequest to_request(const RemapRequest& request) {
    runtime::memory::RemapRequest out;
    out.start = request.start;
    out.size = request.size;
    out.permission = to_permission(request.permission);
    out.preserve_content = request.preserve_content;
    out.anon_name = request.anon_name;
    return out;
}

// runtime 快照转 SDK 快照
VmaSnapshot to_snapshot(const runtime::memory::VmaSnapshot& snapshot) {
    VmaSnapshot out;
    out.id = snapshot.id;
    out.monotonic_time_ns = snapshot.monotonic_time_ns;
    out.entries.reserve(snapshot.entries.size());
    for (const auto& entry : snapshot.entries) {
        out.entries.push_back(detail::to_entry(entry));
    }
    return out;
}

// SDK 快照转 runtime 快照
runtime::memory::VmaSnapshot to_snapshot(const VmaSnapshot& snapshot) {
    runtime::memory::VmaSnapshot out;
    out.id = snapshot.id;
    out.monotonic_time_ns = snapshot.monotonic_time_ns;
    out.entries.reserve(snapshot.entries.size());
    for (const auto& entry : snapshot.entries) {
        out.entries.push_back(to_entry(entry));
    }
    return out;
}

// runtime 差异记录转 SDK 差异记录
VmaDiff to_diff(const runtime::memory::VmaDiff& diff) {
    VmaDiff out;
    out.before = to_snapshot(diff.before);
    out.after = to_snapshot(diff.after);
    out.entries.reserve(diff.entries.size());
    for (const auto& entry : diff.entries) {
        out.entries.push_back(VmaDiffEntry{
            to_kind(entry.kind),
            detail::to_entry(entry.before),
            detail::to_entry(entry.after),
            entry.detail
        });
    }
    return out;
}

// runtime 操作记录转 SDK 操作记录
OperationRecord to_record(const runtime::memory::VmaOperationRecord& record) {
    OperationRecord out;
    out.id = record.id;
    out.transaction_id = record.transaction_id;
    out.kind = to_kind(record.kind);
    out.status = to_status(record.status);
    out.before = detail::to_entry(record.before);
    out.after = detail::to_entry(record.after);
    out.original_protection = to_options(record.original_protection);
    out.applied_protection = to_options(record.applied_protection);
    out.anon_name = record.anon_name;
    out.advice = record.advice;
    out.mremap_flags = record.mremap_flags;
    out.old_address = record.old_address;
    out.new_address = record.new_address;
    out.old_size = record.old_size;
    out.new_size = record.new_size;
    out.result = bridge::result_from(record.result);
    return out;
}

// 填充 SDK maps 条目列表
void fill_entries(const std::vector<runtime::memory::MemoryMapEntry>& entries, std::vector<MapEntry>* out) {
    out->clear();
    out->reserve(entries.size());
    for (const auto& entry : entries) {
        out->push_back(detail::to_entry(entry));
    }
}

// 填充 SDK 操作记录列表
void fill_records(const std::vector<runtime::memory::VmaOperationRecord>& records, std::vector<OperationRecord>* out) {
    out->clear();
    out->reserve(records.size());
    for (const auto& record : records) {
        out->push_back(to_record(record));
    }
}

// 填充单条 SDK 操作记录
void fill_single_record(
    const std::vector<runtime::memory::VmaOperationRecord>& records,
    OperationRecord* out
) {
    if (out == nullptr) {
        return;
    }
    *out = records.empty() ? OperationRecord{} : to_record(records.front());
}

// 检查地址加长度是否溢出
bool checked_add(std::uintptr_t start, std::size_t size, std::uintptr_t* out) {
    if (out == nullptr || size > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }

    *out = start + static_cast<std::uintptr_t>(size);
    return true;
}

// 判断两个 VMA 的读写执行权限是否一致
bool same_protection(
    const runtime::memory::MemoryMapEntry& lhs,
    const runtime::memory::MemoryMapEntry& rhs
) {
    return lhs.readable() == rhs.readable() &&
        lhs.writable() == rhs.writable() &&
        lhs.executable() == rhs.executable();
}

// 计算带符号偏移后的地址
bool add_offset(std::uintptr_t addr, std::intptr_t offset, std::uintptr_t* out) {
    if (out == nullptr) {
        return false;
    }

    if (offset >= 0) {
        const auto positive = static_cast<std::uintptr_t>(offset);
        if (positive > std::numeric_limits<std::uintptr_t>::max() - addr) {
            return false;
        }
        *out = addr + positive;
        return *out != 0;
    }

    const auto negative = static_cast<std::uintptr_t>(-(offset + 1)) + 1;
    if (negative > addr) {
        return false;
    }
    *out = addr - negative;
    return *out != 0;
}

// 根据 MemTool 中的 pid、包名或当前进程选择目标进程
Result process_for(const MemTool& mem, runtime::memory::MemProcess* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing memory process output"};
    }

    if (mem.pid > 0) {
        *out = runtime::memory::MemProcess::from_pid(static_cast<pid_t>(mem.pid));
        out->package_name = mem.packageName;
        return Result{};
    }

    if (!mem.packageName.empty()) {
        runtime::memory::MemProcess process;
        auto result = runtime::memory::process_from_package(mem.packageName, &process);
        if (!result.ok()) {
            *out = runtime::memory::MemProcess{};
            return bridge::result_from(result);
        }
        *out = std::move(process);
        return Result{};
    }

    *out = runtime::memory::MemProcess::current();
    return Result{};
}

// SDK 写入策略转 runtime 写入模式
runtime::memory::WriteMode to_mode(WritePolicy policy) {
    switch (policy) {
        case WritePolicy::CurrentPermission:
            return runtime::memory::WriteMode::CurrentPermission;
        case WritePolicy::AutoProtect:
            return runtime::memory::WriteMode::AutoProtect;
        case WritePolicy::SecureWrite:
            return runtime::memory::WriteMode::SecureWrite;
    }

    return runtime::memory::WriteMode::AutoProtect;
}

// 组装 runtime 冻结项
runtime::memory::FreezeItem to_freeze_item(const Item& item, const runtime::memory::ScalarValue& scalar) {
    return runtime::memory::FreezeItem{item.value, item.addr, item.type, scalar};
}

// 停止当前工具拥有的冻结任务
Result stop_tool_freeze(MemTool* mem) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }

    return bridge::result_from(runtime::memory::stop_freeze(mem));
}

// 添加冻结项并同步 SDK 侧缓存
Result add_tool_freeze(
    MemTool* mem,
    const Item& item,
    const runtime::memory::ScalarValue& scalar,
    const runtime::memory::MemProcess& process
) {
    if (mem == nullptr || item.addr == 0) {
        return Result{Status::InvalidArgument, "missing memory tool or freeze address"};
    }

    auto added = runtime::memory::add_freeze(
        mem,
        to_freeze_item(item, scalar),
        process,
        mem->freezeDelayMs
    );
    if (!added.ok()) {
        return bridge::result_from(added);
    }

    mem->freezeItems.erase(
        std::remove_if(
            mem->freezeItems.begin(),
            mem->freezeItems.end(),
            [item](const Item& current) { return current.addr == item.addr; }
        ),
        mem->freezeItems.end()
    );
    mem->freezeItems.push_back(item);
    return Result{};
}

// 检查 SDK 内存区域 ID 是否有效
bool sdk_area_valid(int area) {
    return runtime::memory::is_area_id(area);
}

// 解析模块查询名，兼容 :bss 和 lib.so[n] 写法
std::string module_query_name(const char* module_name, int* ordinal) {
    if (ordinal != nullptr) {
        *ordinal = 1;
    }
    if (module_name == nullptr) {
        return {};
    }

    std::string name(module_name);
    const std::size_t bss = name.find(":bss");
    if (bss != std::string::npos) {
        name.erase(bss);
    }

    const std::size_t open = name.find('[');
    const std::size_t close = name.find(']', open == std::string::npos ? 0 : open);
    if (open != std::string::npos && close != std::string::npos && open < close) {
        if (ordinal != nullptr) {
            const std::string index = name.substr(open + 1, close - open - 1);
            char* end = nullptr;
            const long parsed = std::strtol(index.c_str(), &end, 10);
            if (end != index.c_str() && *end == '\0' && parsed > 0) {
                *ordinal = static_cast<int>(parsed);
            }
        }
        name.erase(open, close - open + 1);
    }

    return name;
}

// 判断 maps 条目是否匹配模块名
bool module_matches(const runtime::memory::MemoryMapEntry& entry, const std::string& name) {
    return !name.empty() && (entry.path.find(name) != std::string::npos || entry.name() == name);
}

// 将地址列表包装为 SDK Value
Value<std::vector<std::uintptr_t>> value_from_results(
    const runtime::RuntimeResult& result,
    const std::vector<std::uintptr_t>& results
) {
    Value<std::vector<std::uintptr_t>> out;
    out.result = bridge::result_from(result);
    out.value = results;
    return out;
}

// 查找当前进程内可写入的 VMA 范围
Result find_write_range(void* addr, std::size_t size, runtime::memory::MemoryMapEntry* out) {
    if (addr == nullptr || size == 0 || out == nullptr) {
        return Result{Status::InvalidArgument, "missing memory write range"};
    }

    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    std::uintptr_t end = 0;
    if (!checked_add(start, size, &end) || end <= start) {
        return Result{Status::InvalidArgument, "memory write range is too large"};
    }

    runtime::memory::MemoryMap memory_map;
    auto result = memory_map.find_address(start, out);
    if (!result.ok()) {
        return bridge::result_from(result);
    }
    if (end > out->end) {
        return Result{Status::InvalidArgument, "memory write range crosses a VMA boundary"};
    }
    return Result{};
}

} // namespace

// 获取当前目标的 maps 列表
Result GetMaps(std::vector<MapEntry>* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing memory map output"};
    }

    runtime::memory::MemoryMap memory_map;
    std::vector<runtime::memory::MemoryMapEntry> entries;
    const auto result = memory_map.current(&entries);
    if (!result.ok()) {
        out->clear();
        return bridge::result_from(result);
    }

    fill_entries(entries, out);
    return bridge::result_from(result);
}

// 获取 maps 并把结果包装为 Value
Value<std::vector<MapEntry>> TryGetMaps() {
    Value<std::vector<MapEntry>> out;
    out.result = GetMaps(&out.value);
    return out;
}

// 获取 maps 的简化接口
std::vector<MapEntry> GetMaps() {
    auto value = TryGetMaps();
    if (!value.ok()) {
        NYX_LOGW("memory GetMaps failed: %s", value.result.detail.c_str());
        return {};
    }
    return std::move(value.value);
}

// 获取当前目标的库列表
Result GetLibs(std::vector<Library>* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing library output"};
    }

    runtime::memory::MemoryMap memory_map;
    std::vector<runtime::memory::MemoryLibrary> libraries;
    const auto result = memory_map.libraries(&libraries);
    if (!result.ok()) {
        out->clear();
        return bridge::result_from(result);
    }

    out->clear();
    out->reserve(libraries.size());
    for (const auto& library : libraries) {
        out->push_back(to_library(library));
    }
    return bridge::result_from(result);
}

// 获取库列表并把结果包装为 Value
Value<std::vector<Library>> TryGetLibs() {
    Value<std::vector<Library>> out;
    out.result = GetLibs(&out.value);
    return out;
}

// 获取库列表的简化接口
std::vector<Library> GetLibs() {
    auto value = TryGetLibs();
    if (!value.ok()) {
        NYX_LOGW("memory GetLibs failed: %s", value.result.detail.c_str());
        return {};
    }
    return std::move(value.value);
}

// 查询地址所在 maps 条目
Result FindAddr(std::uintptr_t address, MapEntry* out) {
    if (address == 0 || out == nullptr) {
        return Result{Status::InvalidArgument, "missing address or memory map output"};
    }

    runtime::memory::MemoryMap memory_map;
    runtime::memory::MemoryMapEntry entry;
    const auto result = memory_map.find_address(address, &entry);
    if (!result.ok()) {
        *out = MapEntry{};
        return bridge::result_from(result);
    }

    *out = detail::to_entry(entry);
    return bridge::result_from(result);
}

// 查询指定库的 maps 条目
Result FindLib(const char* name, std::vector<MapEntry>* out) {
    if (name == nullptr || name[0] == '\0' || out == nullptr) {
        return Result{Status::InvalidArgument, "missing library name or memory map output"};
    }

    runtime::memory::MemoryMap memory_map;
    std::vector<runtime::memory::MemoryMapEntry> entries;
    const auto result = memory_map.find_library(name, &entries);
    if (!result.ok()) {
        out->clear();
        return bridge::result_from(result);
    }

    fill_entries(entries, out);
    return bridge::result_from(result);
}

// 捕获 VMA 快照
Result Snapshot(VmaSnapshot* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing vma snapshot output"};
    }

    runtime::memory::MemoryNormalizer normalizer;
    runtime::memory::VmaSnapshot snapshot;
    const auto result = normalizer.snapshot(&snapshot);
    if (!result.ok()) {
        *out = VmaSnapshot{};
        return bridge::result_from(result);
    }

    *out = to_snapshot(snapshot);
    return bridge::result_from(result);
}

// 计算两个 VMA 快照的差异
Result Diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing vma diff output"};
    }

    runtime::memory::MemoryNormalizer normalizer;
    runtime::memory::VmaDiff diff;
    const auto result = normalizer.diff(to_snapshot(before), to_snapshot(after), &diff);
    if (!result.ok()) {
        *out = VmaDiff{};
        return bridge::result_from(result);
    }

    *out = to_diff(diff);
    return bridge::result_from(result);
}

// 通过事务提交单次权限保护
Result Protect(const ProtectOptions& options, OperationRecord* out) {
    runtime::memory::VmaTransaction transaction;
    auto added = transaction.add_protect(to_request(options));
    if (!added.ok()) {
        if (out != nullptr) {
            *out = OperationRecord{};
        }
        return bridge::result_from(added);
    }

    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = transaction.commit(&records);
    fill_single_record(records, out);
    return bridge::result_from(result);
}

// 按布尔权限保护内存区域
Result Protect(void* addr, std::size_t len, bool read, bool write, bool execute) {
    return Protect(addr, len, PagePermission{read, write, execute});
}

// 按权限结构保护内存区域
Result Protect(void* addr, std::size_t len, PagePermission permission, OperationRecord* out) {
    if (addr == nullptr || len == 0) {
        return Result{Status::InvalidArgument, "missing memory protection range"};
    }

    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    std::uintptr_t end = 0;
    if (!checked_add(start, len, &end) || end <= start) {
        return Result{Status::InvalidArgument, "memory protection range is too large"};
    }

    return Protect(ProtectOptions{start, end, permission.read, permission.write, permission.execute}, out);
}

// 保护指定库代码段
Result ProtectLib(const char* name, std::vector<OperationRecord>* out) {
    if (name == nullptr || name[0] == '\0' || out == nullptr) {
        return Result{Status::InvalidArgument, "missing library name or vma operation output"};
    }

    runtime::memory::MemoryNormalizer normalizer;
    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = normalizer.protect_library_code(name, &records);
    fill_records(records, out);
    return bridge::result_from(result);
}

// 写入当前进程内存，必要时临时放开页权限
Result Write(void* addr, const void* data, std::size_t size, WritePolicy policy) {
    if (data == nullptr) {
        return Result{Status::InvalidArgument, "missing memory write data"};
    }

    runtime::memory::MemoryMapEntry entry;
    auto range = find_write_range(addr, size, &entry);
    if (!range.ok()) {
        return range;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(addr);
    const auto end = start + static_cast<std::uintptr_t>(size);
    // 当前权限模式只允许写本来可写的 VMA
    if (policy == WritePolicy::CurrentPermission) {
        if (!entry.writable()) {
            return Result{Status::Denied, "memory write range is not writable"};
        }

        std::memcpy(addr, data, size);
        if (entry.executable()) {
            __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(end));
        }
        return Result{};
    }

    // AutoProtect 和 SecureWrite 都先放开写权限再恢复
    const bool secure = policy == WritePolicy::SecureWrite;
    runtime::memory::PageProtection protection;
    auto unlock = protection.set(runtime::memory::PageProtectRequest{
        start,
        end,
        true,
        true,
        entry.executable()
    });
    if (!unlock.ok()) {
        return bridge::result_from(unlock);
    }

    if (secure) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    std::memcpy(addr, data, size);
    if (entry.executable()) {
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(end));
    }
    if (secure) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    auto restore = protection.set(runtime::memory::PageProtectRequest{
        start,
        end,
        entry.readable(),
        entry.writable(),
        entry.executable()
    });
    if (!restore.ok()) {
        return bridge::result_from(restore);
    }

    if (secure) {
        // SecureWrite 额外确认页权限确实恢复到写入前
        runtime::memory::MemoryMapEntry restored;
        runtime::memory::MemoryMap map;
        auto refound = map.find_address(start, &restored);
        if (!refound.ok()) {
            return bridge::result_from(refound);
        }
        if (!same_protection(entry, restored)) {
            return Result{
                Status::Failed,
                "secure memory write did not restore the original page protection"
            };
        }
    }

    return Result{};
}

// 按包名查找 pid
Value<int> getPID(const char* packageName) {
    Value<int> out;
    if (packageName == nullptr || packageName[0] == '\0') {
        out.result = Result{Status::InvalidArgument, "missing package name"};
        return out;
    }

    pid_t pid = 0;
    auto result = runtime::memory::find_pid(packageName, &pid);
    out.result = bridge::result_from(result);
    out.value = static_cast<int>(pid);
    return out;
}

// 将 MemTool 绑定到包名对应的进程
Result setPackageName(MemTool* mem, const char* packageName) {
    if (mem == nullptr || packageName == nullptr || packageName[0] == '\0') {
        return Result{Status::InvalidArgument, "missing memory tool or package name"};
    }

    stop_tool_freeze(mem);
    runtime::memory::MemProcess process;
    auto result = runtime::memory::process_from_package(packageName, &process);
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    mem->pid = static_cast<int>(process.pid);
    mem->packageName = packageName;
    mem->results.clear();
    mem->freezeItems.clear();
    return Result{};
}

// 将 MemTool 绑定到指定 pid
Result setPid(MemTool* mem, int pid) {
    if (mem == nullptr || pid <= 0) {
        return Result{Status::InvalidArgument, "missing memory tool or pid"};
    }

    stop_tool_freeze(mem);
    mem->pid = pid;
    mem->packageName.clear();
    mem->results.clear();
    mem->freezeItems.clear();
    return Result{};
}

// 设置预定义内存区域
Result setArea(MemTool* mem, int memoryArea) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }
    if (!sdk_area_valid(memoryArea)) {
        return Result{Status::InvalidArgument, "invalid memory area"};
    }

    mem->memoryArea = memoryArea;
    mem->memoryMaps.clear();
    return Result{};
}

// 设置自定义 maps 过滤条件
Result setArea(MemTool* mem, const char* memoryMaps) {
    if (mem == nullptr || memoryMaps == nullptr || memoryMaps[0] == '\0') {
        return Result{Status::InvalidArgument, "missing memory tool or custom maps filter"};
    }

    mem->memoryMaps = memoryMaps;
    return Result{};
}

// 查询模块基址，支持 XA、CD、CB 三种头部类型
Value<std::uintptr_t> getModuleBaseAddr(const MemTool& mem, const char* moduleName, int headType) {
    Value<std::uintptr_t> out;
    runtime::memory::MemProcess process;
    out.result = process_for(mem, &process);
    if (!out.ok()) {
        return out;
    }

    if (headType != HEAD_XA && headType != HEAD_CD && headType != HEAD_CB) {
        out.result = Result{Status::InvalidArgument, "invalid module head type"};
        return out;
    }

    // 自定义 maps 过滤优先于模块名查询
    if (!mem.memoryMaps.empty()) {
        runtime::memory::MemoryMap map(process);
        std::vector<runtime::memory::MemoryMapEntry> entries;
        auto maps = map.current(&entries);
        if (!maps.ok()) {
            out.result = bridge::result_from(maps);
            return out;
        }
        for (const auto& entry : entries) {
            if (runtime::memory::matches_custom_area(entry, mem.memoryMaps)) {
                out.value = entry.start == 0x8000 ? 0 : entry.start;
                return out;
            }
        }
        out.result = Result{Status::NotFound, "custom memory map filter was not found"};
        return out;
    }

    // 模块名可携带 [n] 指定第 n 个匹配项
    int ordinal = 1;
    const std::string module = module_query_name(moduleName, &ordinal);
    if (module.empty()) {
        out.result = Result{Status::InvalidArgument, "missing module name"};
        return out;
    }

    runtime::memory::MemoryMap map(process);
    std::vector<runtime::memory::MemoryMapEntry> entries;
    auto maps = map.current(&entries);
    if (!maps.ok()) {
        out.result = bridge::result_from(maps);
        return out;
    }

    bool seen_xa = false;
    int index = 1;
    for (const auto& entry : entries) {
        // HEAD_XA 直接找应用代码段
        if (headType == HEAD_XA) {
            if (module_matches(entry, module) && runtime::memory::matches_area(entry, RANGE_CODE_APP)) {
                if (index++ != ordinal) {
                    continue;
                }
                out.value = entry.start == 0x8000 ? 0 : entry.start;
                return out;
            }
            continue;
        }

        // HEAD_CD 和 HEAD_CB 从命中的 XA 后继续寻找相邻数据段
        if (module_matches(entry, module) && runtime::memory::matches_area(entry, RANGE_CODE_APP)) {
            seen_xa = true;
        }
        if (!seen_xa) {
            continue;
        }

        const bool matches = headType == HEAD_CD
            ? module_matches(entry, module) && runtime::memory::matches_area(entry, RANGE_C_DATA)
            : runtime::memory::matches_area(entry, RANGE_C_BSS);
        if (!matches) {
            continue;
        }
        if (index++ != ordinal) {
            continue;
        }
        out.value = entry.start;
        return out;
    }

    out.result = Result{Status::NotFound, "module base address was not found"};
    return out;
}

// 读取指针宽度的数据作为跳转地址
Value<std::uintptr_t> jump(const MemTool& mem, std::uintptr_t addr, int count) {
    Value<std::uintptr_t> out;
    if (count <= 0 || static_cast<std::size_t>(count) > sizeof(std::uintptr_t)) {
        out.result = Result{Status::InvalidArgument, "invalid pointer read width"};
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(mem, &process);
    if (!out.ok()) {
        return out;
    }

    runtime::memory::MemoryReader reader(process);
    std::uintptr_t value = 0;
    auto read = reader.read(addr, &value, static_cast<std::size_t>(count));
    out.result = bridge::result_from(read);
    out.value = value;
    return out;
}

// 读取 32 位指针
Value<std::uintptr_t> jump32(const MemTool& mem, std::uintptr_t addr) {
    return jump(mem, addr, 4);
}

// 读取 64 位指针
Value<std::uintptr_t> jump64(const MemTool& mem, std::uintptr_t addr) {
    return jump(mem, addr, 8);
}

// 写入地址值，isFree 为真时转为冻结项
Result setAddrValue(
    MemTool* mem,
    const char* value,
    std::uintptr_t addr,
    int type,
    bool isFree,
    WritePolicy policy
) {
    if (mem == nullptr || addr == 0) {
        return Result{Status::InvalidArgument, "missing memory tool or address"};
    }

    runtime::memory::ScalarValue scalar;
    auto parsed = runtime::memory::parse_value(value, type, &scalar);
    if (!parsed.ok()) {
        return bridge::result_from(parsed);
    }

    runtime::memory::MemProcess process;
    auto process_result = process_for(*mem, &process);
    if (!process_result.ok()) {
        return process_result;
    }

    // 冻结写入需要登记任务并启动冻结循环
    if (isFree) {
        auto added = add_tool_freeze(mem, Item{value, addr, type}, scalar, process);
        if (!added.ok()) {
            return added;
        }
        return startAllFreeze(mem);
    }

    // 普通写入成功后移除同地址冻结项
    runtime::memory::MemoryWriter writer(process);
    auto wrote = writer.write(addr, scalar.bytes.data(), scalar.size, to_mode(policy));
    if (wrote.ok()) {
        mem->freezeItems.erase(
            std::remove_if(
                mem->freezeItems.begin(),
                mem->freezeItems.end(),
                [addr](const Item& item) { return item.addr == addr; }
            ),
            mem->freezeItems.end()
        );
        runtime::memory::remove_freeze(mem, addr);
    }
    return bridge::result_from(wrote);
}

// 读取地址值并格式化为字符串
Value<std::string> getAddrData(const MemTool& mem, std::uintptr_t addr, int type) {
    Value<std::string> out;
    if (addr == 0 || !runtime::memory::is_type_id(type)) {
        out.result = Result{Status::InvalidArgument, "missing address or invalid type"};
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(mem, &process);
    if (!out.ok()) {
        return out;
    }

    std::array<std::uint8_t, 8> buffer{};
    runtime::memory::MemoryReader reader(process);
    auto read = reader.read(addr, buffer.data(), runtime::memory::type_size(type));
    out.result = bridge::result_from(read);
    if (out.ok()) {
        out.value = runtime::memory::format_value(buffer.data(), type);
    }
    return out;
}

// 搜索单值，含分号时转入联合搜索
Value<std::vector<std::uintptr_t>> Search(MemTool* mem, const char* value, int type) {
    if (value != nullptr && std::strchr(value, ';') != nullptr) {
        return SearchUnited(mem, value, type);
    }

    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::SearchTerm term;
    auto parsed = runtime::memory::parse_search(value, type, &term);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    runtime::memory::MemoryScanner scanner(process);
    runtime::memory::ScanRequest request;
    request.area = mem->memoryArea;
    request.custom_area = mem->memoryMaps;
    request.term = term;
    request.max_results = mem->maxResults;
    auto result = scanner.search(request, &mem->results);
    out.result = bridge::result_from(result);
    out.value = mem->results;
    return out;
}

// 搜索范围值
Value<std::vector<std::uintptr_t>> SearchRange(MemTool* mem, const char* value, int type) {
    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::SearchTerm term;
    auto parsed = runtime::memory::parse_range(value, type, &term);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    runtime::memory::MemoryScanner scanner(process);
    runtime::memory::ScanRequest request;
    request.area = mem->memoryArea;
    request.custom_area = mem->memoryMaps;
    request.term = term;
    request.max_results = mem->maxResults;
    auto result = scanner.search(request, &mem->results);
    out.result = bridge::result_from(result);
    out.value = mem->results;
    return out;
}

// 搜索联合条件
Value<std::vector<std::uintptr_t>> SearchUnited(MemTool* mem, const char* value, int type) {
    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::UnitedSearch united;
    auto parsed = runtime::memory::parse_united(value, type, &united);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    runtime::memory::MemoryScanner scanner(process);
    runtime::memory::ScanRequest request;
    request.area = mem->memoryArea;
    request.custom_area = mem->memoryMaps;
    request.term = united.terms.front();
    request.max_results = mem->maxResults;
    auto result = scanner.search_united(request, united, &mem->results);
    out.result = bridge::result_from(result);
    out.value = mem->results;
    return out;
}

// 按偏移筛选已有搜索结果
Value<std::vector<std::uintptr_t>> ImproveOffset(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset
) {
    // 输入中带联合或范围语法时交给对应筛选器
    if (value != nullptr && std::strchr(value, ';') != nullptr) {
        return ImproveOffsetUnited(mem, value, type, offset);
    }
    if (value != nullptr && std::strchr(value, '~') != nullptr) {
        return ImproveOffsetRange(mem, value, type, offset);
    }

    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::SearchTerm term;
    auto parsed = runtime::memory::parse_search(value, type, &term);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    std::vector<std::uintptr_t> filtered;
    runtime::memory::MemoryImprover improver(process);
    auto result = improver.filter(mem->results, term, offset, &filtered);
    out.result = bridge::result_from(result);
    // 只有筛选成功才替换 MemTool 的结果集
    if (result.ok()) {
        mem->results = filtered;
    }
    out.value = result.ok() ? mem->results : filtered;
    return out;
}

// 按范围值筛选已有搜索结果
Value<std::vector<std::uintptr_t>> ImproveOffsetRange(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset
) {
    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::SearchTerm term;
    auto parsed = runtime::memory::parse_range(value, type, &term);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    std::vector<std::uintptr_t> filtered;
    runtime::memory::MemoryImprover improver(process);
    auto result = improver.filter(mem->results, term, offset, &filtered);
    out.result = bridge::result_from(result);
    // 只有筛选成功才替换 MemTool 的结果集
    if (result.ok()) {
        mem->results = filtered;
    }
    out.value = result.ok() ? mem->results : filtered;
    return out;
}

// 按联合条件筛选已有搜索结果
Value<std::vector<std::uintptr_t>> ImproveOffsetUnited(MemTool* mem, const char* value, int type, std::intptr_t offset) {
    Value<std::vector<std::uintptr_t>> out;
    if (mem == nullptr) {
        out.result = Result{Status::InvalidArgument, "missing memory tool"};
        return out;
    }

    runtime::memory::UnitedSearch united;
    auto parsed = runtime::memory::parse_united(value, type, &united);
    if (!parsed.ok()) {
        out.result = bridge::result_from(parsed);
        return out;
    }

    runtime::memory::MemProcess process;
    out.result = process_for(*mem, &process);
    if (!out.ok()) {
        return out;
    }

    std::vector<std::uintptr_t> filtered;
    runtime::memory::MemoryImprover improver(process);
    auto result = improver.filter_united(mem->results, united, offset, &filtered);
    out.result = bridge::result_from(result);
    // 只有筛选成功才替换 MemTool 的结果集
    if (result.ok()) {
        mem->results = filtered;
    }
    out.value = result.ok() ? mem->results : filtered;
    return out;
}

// 用零偏移筛选已有搜索结果
Value<std::vector<std::uintptr_t>> ImproveValue(MemTool* mem, const char* value, int type) {
    return ImproveOffset(mem, value, type, 0);
}

// 对当前结果集按偏移写入或冻结
Result OffsetWrite(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset,
    bool isFree,
    WritePolicy policy
) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }
    if (mem->results.empty()) {
        return Result{Status::NotFound, "memory offset write has no input results"};
    }

    runtime::memory::ScalarValue scalar;
    auto parsed = runtime::memory::parse_value(value, type, &scalar);
    if (!parsed.ok()) {
        return bridge::result_from(parsed);
    }

    runtime::memory::MemProcess process;
    auto process_result = process_for(*mem, &process);
    if (!process_result.ok()) {
        return process_result;
    }

    // 冻结模式逐个登记目标地址，全部成功后启动冻结
    if (isFree) {
        std::size_t ok_count = 0;
        std::size_t fail_count = 0;
        for (std::uintptr_t base : mem->results) {
            std::uintptr_t target = 0;
            if (!add_offset(base, offset, &target)) {
                ++fail_count;
                continue;
            }

            auto added = add_tool_freeze(mem, Item{value, target, type}, scalar, process);
            if (added.ok()) {
                ++ok_count;
            } else {
                ++fail_count;
            }
        }

        if (ok_count != 0) {
            auto started = startAllFreeze(mem);
            if (!started.ok()) {
                return started;
            }
        }
        if (fail_count != 0) {
            std::ostringstream detail;
            detail << "memory offset freeze partially failed: " << ok_count << " succeeded, " << fail_count << " failed";
            return Result{Status::Failed, detail.str()};
        }
        return Result{};
    }

    // 普通写入允许部分成功，并在详情里返回成功/失败数量
    runtime::memory::MemoryWriter writer(process);
    std::size_t ok_count = 0;
    std::size_t fail_count = 0;
    for (std::uintptr_t base : mem->results) {
        std::uintptr_t target = 0;
        if (!add_offset(base, offset, &target)) {
            ++fail_count;
            continue;
        }

        auto wrote = writer.write(target, scalar.bytes.data(), scalar.size, to_mode(policy));
        if (wrote.ok()) {
            mem->freezeItems.erase(
                std::remove_if(
                    mem->freezeItems.begin(),
                    mem->freezeItems.end(),
                    [target](const Item& item) { return item.addr == target; }
                ),
                mem->freezeItems.end()
            );
            runtime::memory::remove_freeze(mem, target);
            ++ok_count;
        } else {
            ++fail_count;
        }
    }

    if (fail_count != 0) {
        std::ostringstream detail;
        detail << "memory offset write partially failed: " << ok_count << " succeeded, " << fail_count << " failed";
        return Result{ok_count == 0 ? Status::Failed : Status::Ok, detail.str()};
    }

    return Result{};
}

// 获取当前结果数量
std::size_t getResultCount(const MemTool& mem) {
    return mem.results.size();
}

// 获取当前结果列表引用
const std::vector<std::uintptr_t>& getResultList(const MemTool& mem) {
    return mem.results;
}

// 清空当前结果列表
Result clearResultList(MemTool* mem) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }

    mem->results.clear();
    return Result{};
}

// 获取当前冻结项列表
Value<std::vector<Item>> getFreezeList(const MemTool& mem) {
    Value<std::vector<Item>> out;
    out.value = mem.freezeItems;
    return out;
}

// 设置冻结循环间隔
Result setFreezeDelayMs(MemTool* mem, std::uint32_t delay) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }
    if (delay == 0) {
        return Result{Status::InvalidArgument, "freeze delay must be greater than zero"};
    }

    mem->freezeDelayMs = delay;
    return bridge::result_from(runtime::memory::set_freeze_delay(mem, delay));
}

// 获取冻结项数量
std::size_t getFreezeNum(const MemTool& mem) {
    return mem.freezeItems.size();
}

// 添加冻结项
Result addFreezeItem(MemTool* mem, const char* value, std::uintptr_t addr, int type) {
    if (mem == nullptr || addr == 0) {
        return Result{Status::InvalidArgument, "missing memory tool or freeze address"};
    }

    runtime::memory::ScalarValue scalar;
    auto parsed = runtime::memory::parse_value(value, type, &scalar);
    if (!parsed.ok()) {
        return bridge::result_from(parsed);
    }

    runtime::memory::MemProcess process;
    auto process_result = process_for(*mem, &process);
    if (!process_result.ok()) {
        return process_result;
    }

    return add_tool_freeze(mem, Item{value, addr, type}, scalar, process);
}

// 移除指定地址的冻结项
Result removeFreezeItem(MemTool* mem, std::uintptr_t addr) {
    if (mem == nullptr || addr == 0) {
        return Result{Status::InvalidArgument, "missing memory tool or freeze address"};
    }

    const auto old_size = mem->freezeItems.size();
    mem->freezeItems.erase(
        std::remove_if(
            mem->freezeItems.begin(),
            mem->freezeItems.end(),
            [addr](const Item& item) { return item.addr == addr; }
        ),
        mem->freezeItems.end()
    );

    runtime::memory::remove_freeze(mem, addr);

    if (mem->freezeItems.size() == old_size) {
        return Result{Status::NotFound, "freeze item was not found"};
    }
    return Result{};
}

// 移除全部冻结项
Result removeAllFreezeItem(MemTool* mem) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }

    mem->freezeItems.clear();
    return bridge::result_from(runtime::memory::clear_freeze(mem));
}

// 启动全部冻结项
Result startAllFreeze(MemTool* mem) {
    if (mem == nullptr) {
        return Result{Status::InvalidArgument, "missing memory tool"};
    }

    runtime::memory::MemProcess process;
    auto process_result = process_for(*mem, &process);
    if (!process_result.ok()) {
        return process_result;
    }

    // 启动前重新解析缓存项，保证 runtime 侧任务完整
    for (const auto& item : mem->freezeItems) {
        runtime::memory::ScalarValue scalar;
        auto parsed = runtime::memory::parse_value(item.value.c_str(), item.type, &scalar);
        if (!parsed.ok()) {
            return bridge::result_from(parsed);
        }
        auto added = runtime::memory::add_freeze(
            mem,
            to_freeze_item(item, scalar),
            process,
            mem->freezeDelayMs
        );
        if (!added.ok()) {
            return bridge::result_from(added);
        }
    }
    return bridge::result_from(runtime::memory::start_freeze(mem, process, mem->freezeDelayMs));
}

// 停止全部冻结项
Result stopAllFreeze(MemTool* mem) {
    return stop_tool_freeze(mem);
}

// 设置匿名 VMA 名称
Result SetName(
    std::uintptr_t start,
    std::size_t size,
    const char* name,
    OperationRecord* out
) {
    if (name == nullptr || name[0] == '\0') {
        return Result{Status::InvalidArgument, "missing anonymous vma name"};
    }

    runtime::memory::VmaTransaction transaction;
    auto added = transaction.add_anon_name(start, size, name);
    if (!added.ok()) {
        fill_single_record(std::vector<runtime::memory::VmaOperationRecord>{}, out);
        return bridge::result_from(added);
    }

    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = transaction.commit(&records);
    fill_single_record(records, out);
    return bridge::result_from(result);
}

// 对 VMA 执行 madvise
Result Advise(std::uintptr_t start, std::size_t size, int advice, OperationRecord* out) {
    runtime::memory::VmaTransaction transaction;
    auto added = transaction.add_advise(start, size, advice);
    if (!added.ok()) {
        fill_single_record(std::vector<runtime::memory::VmaOperationRecord>{}, out);
        return bridge::result_from(added);
    }

    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = transaction.commit(&records);
    fill_single_record(records, out);
    return bridge::result_from(result);
}

// 调整 VMA 大小
Result Resize(
    std::uintptr_t start,
    std::size_t old_size,
    std::size_t new_size,
    int flags,
    std::uintptr_t* out_new_address,
    OperationRecord* out
) {
    if (out_new_address == nullptr) {
        return Result{Status::InvalidArgument, "missing resize output address"};
    }
    *out_new_address = 0;

    runtime::memory::VmaTransaction transaction;
    auto added = transaction.add_resize(start, old_size, new_size, flags);
    if (!added.ok()) {
        fill_single_record(std::vector<runtime::memory::VmaOperationRecord>{}, out);
        return bridge::result_from(added);
    }

    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = transaction.commit(&records);
    if (!records.empty()) {
        *out_new_address = records.front().new_address;
    }
    fill_single_record(records, out);
    return bridge::result_from(result);
}

// 重映射 VMA
Result Remap(const RemapRequest& request, std::uintptr_t* out_new_address, OperationRecord* out) {
    if (out_new_address == nullptr) {
        return Result{Status::InvalidArgument, "missing remap output address"};
    }
    *out_new_address = 0;

    runtime::memory::VmaTransaction transaction;
    auto added = transaction.add_remap(to_request(request));
    if (!added.ok()) {
        fill_single_record(std::vector<runtime::memory::VmaOperationRecord>{}, out);
        return bridge::result_from(added);
    }

    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = transaction.commit(&records);
    if (!records.empty()) {
        *out_new_address = records.front().new_address;
    }
    fill_single_record(records, out);
    return bridge::result_from(result);
}

// 回滚指定事务
Result Rollback(std::uint64_t transaction_id, std::vector<OperationRecord>* out) {
    if (out == nullptr) {
        return Result{Status::InvalidArgument, "missing rollback output records"};
    }

    runtime::memory::MemoryNormalizer normalizer;
    std::vector<runtime::memory::VmaOperationRecord> records;
    const auto result = normalizer.rollback(transaction_id, &records);
    fill_records(records, out);
    return bridge::result_from(result);
}

} // namespace memory
} // namespace sdk
} // namespace nyx
