#include "sdk/include/vfs_patcher.h"

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_protect.h"
#include "sdk/result_bridge.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nyx {
namespace sdk {
namespace vfs {

namespace {

// 检查地址加长度是否溢出
bool checked_add(std::uintptr_t lhs, std::size_t rhs, std::uintptr_t* out) {
    if (out == nullptr || rhs > std::numeric_limits<std::uintptr_t>::max() - lhs) {
        return false;
    }

    *out = lhs + static_cast<std::uintptr_t>(rhs);
    return true;
}

// 将模块文件偏移范围解析为当前进程中的运行时地址范围
bool resolve_file_range(
    const std::vector<runtime::memory::MemoryMapEntry>& entries,
    std::uintptr_t offset,
    std::uintptr_t offset_end,
    runtime::memory::MemoryMapEntry* out,
    std::uintptr_t* runtime_start,
    std::uintptr_t* runtime_end
) {
    runtime::memory::MemoryMapEntry best;
    std::uintptr_t best_start = 0;
    std::uintptr_t best_end = 0;
    bool found = false;

    for (const auto& entry : entries) {
        if (offset < entry.offset) {
            continue;
        }

        std::uintptr_t entry_offset_end = 0;
        if (!checked_add(entry.offset, entry.size(), &entry_offset_end) ||
            offset_end > entry_offset_end) {
            continue;
        }

        const std::uintptr_t delta = offset - entry.offset;
        std::uintptr_t start = 0;
        if (!checked_add(entry.start, static_cast<std::size_t>(delta), &start)) {
            continue;
        }

        std::uintptr_t end = 0;
        if (!checked_add(start, static_cast<std::size_t>(offset_end - offset), &end) ||
            end <= start ||
            end > entry.end) {
            continue;
        }

        // 同一文件偏移可能映射到多个段，优先选择最适合直接补丁的段
        const int rank =
            entry.writable() ? 3 :
            entry.executable() ? 2 :
            entry.readable() ? 1 : 0;
        const int best_rank =
            best.writable() ? 3 :
            best.executable() ? 2 :
            best.readable() ? 1 : 0;
        if (!found ||
            rank > best_rank ||
            (rank == best_rank && entry.offset > best.offset) ||
            (rank == best_rank && entry.offset == best.offset && entry.start > best.start)) {
            best = entry;
            best_start = start;
            best_end = end;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    if (out != nullptr) {
        *out = best;
    }
    if (runtime_start != nullptr) {
        *runtime_start = best_start;
    }
    if (runtime_end != nullptr) {
        *runtime_end = best_end;
    }
    return true;
}

// 补丁 ID 计数器
std::atomic<std::uint64_t>& patch_ids() {
    static std::atomic<std::uint64_t> next{1};
    return next;
}

// tracked patch 表锁
std::mutex& tracked_mutex() {
    static std::mutex value;
    return value;
}

// tracked patch 记录表
std::unordered_map<PatchId, ModulePatchRecord>& tracked_patches() {
    static std::unordered_map<PatchId, ModulePatchRecord> value;
    return value;
}

// maps 条目转补丁段信息
PatchSegment to_segment(const runtime::memory::MemoryMapEntry& entry) {
    return PatchSegment{
        entry.start,
        entry.end,
        entry.permissions,
        entry.offset,
        entry.path
    };
}

// 判断补丁段是否可读
bool segment_readable(const PatchSegment& segment) {
    return !segment.permissions.empty() && segment.permissions[0] == 'r';
}

// 判断补丁段是否可写
bool segment_writable(const PatchSegment& segment) {
    return segment.permissions.size() > 1 && segment.permissions[1] == 'w';
}

// 判断补丁段是否可执行
bool segment_executable(const PatchSegment& segment) {
    return segment.permissions.size() > 2 && segment.permissions[2] == 'x';
}

// 写入补丁记录状态和结果
Result record_result(ModulePatchRecord* record, PatchStatus status, Result result) {
    if (record != nullptr) {
        record->status = status;
        record->result = result;
    }
    return result;
}

// 写入 runtime 结果到补丁记录
Result record_runtime_result(ModulePatchRecord* record, PatchStatus status, const runtime::RuntimeResult& result) {
    return record_result(record, status, bridge::result_from(result));
}

// 执行模块内存补丁并可选记录回滚信息
Result patch_module_record(
    const char* module,
    std::uintptr_t offset,
    const void* data,
    std::size_t size,
    ModulePatchRecord* out
) {
    if (out != nullptr) {
        *out = ModulePatchRecord{};
        out->id = patch_ids().fetch_add(1, std::memory_order_relaxed);
        out->module = module != nullptr ? module : "";
        out->file_offset = offset;
    }

    if (module == nullptr || module[0] == '\0' || data == nullptr || size == 0) {
        return record_result(out, PatchStatus::Failed, Result{Status::InvalidArgument, "missing patch module input"});
    }

    runtime::memory::MemoryMap memory_map;
    std::vector<runtime::memory::MemoryMapEntry> entries;
    auto find = memory_map.find_library(module, &entries);
    if (!find.ok()) {
        return record_runtime_result(out, PatchStatus::Failed, find);
    }

    std::uintptr_t offset_end = 0;
    if (!checked_add(offset, size, &offset_end) || offset_end <= offset) {
        return record_result(out, PatchStatus::Failed, Result{Status::InvalidArgument, "module patch range is too large"});
    }

    runtime::memory::MemoryMapEntry segment;
    std::uintptr_t patch_start = 0;
    std::uintptr_t patch_end = 0;
    if (!resolve_file_range(entries, offset, offset_end, &segment, &patch_start, &patch_end)) {
        return record_result(out, PatchStatus::Failed, Result{Status::NotFound, "module patch range is not mapped"});
    }

    if (out != nullptr) {
        out->runtime_address = patch_start;
        out->segment = to_segment(segment);
        out->after.assign(
            static_cast<const std::uint8_t*>(data),
            static_cast<const std::uint8_t*>(data) + size
        );
    }

    runtime::memory::PageProtection protection;
    // 写入前临时放开目标页读写权限，保留原执行属性
    const auto unlock = protection.set(runtime::memory::PageProtectRequest{
        patch_start,
        patch_end,
        true,
        true,
        segment.executable()
    });
    if (!unlock.ok()) {
        return record_runtime_result(out, PatchStatus::Failed, unlock);
    }

    if (out != nullptr) {
        // 记录原始字节，供 tracked rollback 使用
        const auto* before = reinterpret_cast<const std::uint8_t*>(patch_start);
        out->before.assign(before, before + size);
    }

    std::memcpy(reinterpret_cast<void*>(patch_start), data, size);
    __builtin___clear_cache(
        reinterpret_cast<char*>(patch_start),
        reinterpret_cast<char*>(patch_end)
    );

    const auto restore = protection.set(runtime::memory::PageProtectRequest{
        patch_start,
        patch_end,
        segment.readable(),
        segment.writable(),
        segment.executable()
    });
    if (!restore.ok()) {
        return record_runtime_result(out, PatchStatus::Failed, restore);
    }

    return record_result(out, PatchStatus::Applied, Result{});
}

} // namespace

// 执行一次性模块补丁
Result Patch(const char* module, std::uintptr_t offset, const void* data, std::size_t size) {
    return patch_module_record(module, offset, data, size, nullptr);
}

// 执行模块补丁并返回补丁记录
Result Patch(
    const char* module,
    std::uintptr_t offset,
    const void* data,
    std::size_t size,
    ModulePatchRecord* out
) {
    return patch_module_record(module, offset, data, size, out);
}

// 执行模块补丁并登记为可按 ID 回滚
Value<PatchId> PatchTracked(
    const char* module,
    std::uintptr_t offset,
    const void* data,
    std::size_t size
) {
    Value<PatchId> out;
    ModulePatchRecord record;
    out.result = Patch(module, offset, data, size, &record);
    if (!out.result.ok()) {
        return out;
    }

    out.value = record.id;
    std::lock_guard<std::mutex> lock(tracked_mutex());
    tracked_patches()[out.value] = std::move(record);
    return out;
}

// 按 tracked patch ID 回滚
Result Rollback(PatchId id) {
    if (id == 0) {
        return Result{Status::InvalidArgument, "missing tracked patch id"};
    }

    ModulePatchRecord record;
    {
        std::lock_guard<std::mutex> lock(tracked_mutex());
        auto found = tracked_patches().find(id);
        if (found == tracked_patches().end()) {
            return Result{Status::InvalidArgument, "tracked patch id is not active"};
        }
        record = found->second;
    }

    auto result = Rollback(&record);

    // 回滚失败时写回更新后的记录，保留失败状态供后续排查
    std::lock_guard<std::mutex> lock(tracked_mutex());
    if (result.ok()) {
        tracked_patches().erase(id);
    } else {
        tracked_patches()[id] = std::move(record);
    }
    return result;
}

// 按补丁记录回滚模块字节
Result Rollback(ModulePatchRecord* record) {
    if (record == nullptr) {
        return Result{Status::InvalidArgument, "missing module patch record"};
    }
    if (record->runtime_address == 0 || record->before.empty()) {
        return record_result(record, PatchStatus::RollbackFailed, Result{
            Status::InvalidArgument,
            "module patch record cannot be rolled back"
        });
    }

    std::uintptr_t patch_end = 0;
    if (!checked_add(record->runtime_address, record->before.size(), &patch_end) ||
        patch_end <= record->runtime_address) {
        return record_result(record, PatchStatus::RollbackFailed, Result{
            Status::InvalidArgument,
            "module patch rollback range is too large"
        });
    }

    runtime::memory::MemoryMap memory_map;
    runtime::memory::MemoryMapEntry segment;
    auto find = memory_map.find_address(record->runtime_address, &segment);
    if (!find.ok()) {
        return record_runtime_result(record, PatchStatus::RollbackFailed, find);
    }
    if (patch_end > segment.end) {
        return record_result(record, PatchStatus::RollbackFailed, Result{
            Status::NotFound,
            "module patch rollback range is not mapped"
        });
    }

    // 优先使用补丁时记录的段权限，记录缺失时退回当前 maps 权限
    const bool restore_read = record->segment.permissions.empty() ? segment.readable() : segment_readable(record->segment);
    const bool restore_write = record->segment.permissions.empty() ? segment.writable() : segment_writable(record->segment);
    const bool restore_execute = record->segment.permissions.empty() ? segment.executable() : segment_executable(record->segment);

    runtime::memory::PageProtection protection;
    // 回滚写入前临时放开目标页读写权限
    const auto unlock = protection.set(runtime::memory::PageProtectRequest{
        record->runtime_address,
        patch_end,
        true,
        true,
        segment.executable()
    });
    if (!unlock.ok()) {
        return record_runtime_result(record, PatchStatus::RollbackFailed, unlock);
    }

    std::memcpy(
        reinterpret_cast<void*>(record->runtime_address),
        record->before.data(),
        record->before.size()
    );
    __builtin___clear_cache(
        reinterpret_cast<char*>(record->runtime_address),
        reinterpret_cast<char*>(patch_end)
    );

    const auto restore = protection.set(runtime::memory::PageProtectRequest{
        record->runtime_address,
        patch_end,
        restore_read,
        restore_write,
        restore_execute
    });
    if (!restore.ok()) {
        return record_runtime_result(record, PatchStatus::RollbackFailed, restore);
    }

    return record_result(record, PatchStatus::RolledBack, Result{});
}

} // namespace vfs
} // namespace sdk
} // namespace nyx
