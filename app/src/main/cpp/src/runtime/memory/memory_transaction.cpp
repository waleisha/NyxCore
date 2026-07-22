#include "src/runtime/memory/memory_transaction.h"

#include <cerrno>
#include <cstring>
#include <mutex>
#include <sys/mman.h>

#include <atomic>
#include <string>
#include <unordered_map>
#include <utility>

#ifndef MREMAP_MAYMOVE
#define MREMAP_MAYMOVE 1
#endif

namespace nyx {
namespace runtime {
namespace memory {

namespace detail {

RuntimeResult find_containing_range(std::uintptr_t start, std::uintptr_t end, MemoryMapEntry* out);
RuntimeResult find_containing_size(std::uintptr_t start, std::size_t size, MemoryMapEntry* out);

} // namespace detail

namespace {

// 事务 ID 计数器
std::atomic<std::uint64_t> g_transaction_id{0};
// 操作 ID 计数器
std::atomic<std::uint64_t> g_operation_id{0};
// 已提交事务历史锁
std::mutex g_history_mutex;
// 已提交事务历史记录
std::unordered_map<std::uint64_t, std::vector<VmaOperationRecord>> g_history;

// 最多保留的事务历史数量
constexpr std::size_t kMaxHistoryEntries = 256;

// 拼接 errno 失败详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "vma transaction failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

// 解析 maps 中的匿名 VMA 名称
bool is_anon_name(const std::string& path, std::string* out) {
    constexpr const char* prefix = "[anon:";
    if (path.rfind(prefix, 0) != 0 || path.empty() || path.back() != ']') {
        return false;
    }

    if (out != nullptr) {
        out->assign(path.begin() + std::strlen(prefix), path.end() - 1);
    }
    return true;
}

// 捕获指定地址提交后的 VMA 状态
RuntimeResult capture_after(std::uintptr_t address, MemoryMapEntry* out) {
    if (address == 0 || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid vma capture address"};
    }

    MemoryMap memory_map;
    return memory_map.find_address(address, out);
}

// 生成回滚到原权限所需的请求
PageProtectRequest original_request(
    const PageProtectRequest& request,
    const MemoryMapEntry& before
) {
    return PageProtectRequest{
        request.start,
        request.end,
        before.readable(),
        before.writable(),
        before.executable()
    };
}

// 保存事务历史，超过上限时淘汰一条旧记录
void save_history(const std::vector<VmaOperationRecord>& records) {
    if (records.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_history_mutex);
    if (g_history.size() >= kMaxHistoryEntries) {
        g_history.erase(g_history.begin());
    }
    g_history[records.front().transaction_id] = records;
}

// 提交权限变更操作
RuntimeResult commit_protect(VmaOperationRecord* record) {
    auto found = detail::find_containing_range(record->applied_protection.start, record->applied_protection.end, &record->before);
    if (!found.ok()) {
        record->status = VmaOperationStatus::Failed;
        record->result = found;
        return found;
    }

    record->original_protection = original_request(record->applied_protection, record->before);

    PageProtection protection;
    auto result = protection.set(record->applied_protection);
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::Failed;
        return result;
    }

    capture_after(record->applied_protection.start, &record->after);
    record->status = VmaOperationStatus::Applied;
    return RuntimeResult{};
}

// 提交匿名 VMA 命名操作
RuntimeResult commit_anon_name(VmaOperationRecord* record) {
    auto found = detail::find_containing_size(record->old_address, record->old_size, &record->before);
    if (!found.ok()) {
        record->status = VmaOperationStatus::Failed;
        record->result = found;
        return found;
    }

    VmaAttributes attributes;
    auto result = attributes.set_anon_name(record->old_address, record->old_size, record->anon_name);
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::Failed;
        return result;
    }

    capture_after(record->old_address, &record->after);
    record->status = VmaOperationStatus::Applied;
    return RuntimeResult{};
}

// 提交 madvise 操作
RuntimeResult commit_advise(VmaOperationRecord* record) {
    auto found = detail::find_containing_size(record->old_address, record->old_size, &record->before);
    if (!found.ok()) {
        record->status = VmaOperationStatus::Failed;
        record->result = found;
        return found;
    }

    VmaAttributes attributes;
    auto result = attributes.advise(record->old_address, record->old_size, record->advice);
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::Failed;
        return result;
    }

    if (!capture_after(record->old_address, &record->after).ok()) {
        record->after = record->before;
    }
    record->status = VmaOperationStatus::Applied;
    return RuntimeResult{};
}

// 提交 mremap 尺寸调整操作
RuntimeResult commit_resize(VmaOperationRecord* record) {
    auto found = detail::find_containing_size(record->old_address, record->old_size, &record->before);
    if (!found.ok()) {
        record->status = VmaOperationStatus::Failed;
        record->result = found;
        return found;
    }

    VmaAttributes attributes;
    std::uintptr_t new_address = 0;
    auto result = attributes.resize(
        record->old_address,
        record->old_size,
        record->new_size,
        record->mremap_flags,
        &new_address
    );
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::Failed;
        return result;
    }

    record->new_address = new_address;
    capture_after(new_address, &record->after);
    record->status = VmaOperationStatus::Applied;
    return RuntimeResult{};
}

// 提交 VMA 重映射操作
RuntimeResult commit_remap(VmaOperationRecord* record) {
    auto found = detail::find_containing_size(record->remap_request.start, record->remap_request.size, &record->before);
    if (!found.ok()) {
        record->status = VmaOperationStatus::Failed;
        record->result = found;
        return found;
    }

    VmaAttributes attributes;
    std::uintptr_t new_address = 0;
    auto result = attributes.remap(record->remap_request, &new_address);
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::Failed;
        return result;
    }

    record->old_address = record->remap_request.start;
    record->old_size = record->remap_request.size;
    record->new_address = new_address;
    record->new_size = record->remap_request.size;
    capture_after(new_address, &record->after);
    record->status = VmaOperationStatus::Applied;
    return RuntimeResult{};
}

// 按操作类型分发提交逻辑
RuntimeResult commit_record(VmaOperationRecord* record) {
    switch (record->kind) {
        case VmaOperationKind::Protect:
            return commit_protect(record);
        case VmaOperationKind::SetName:
            return commit_anon_name(record);
        case VmaOperationKind::Advise:
            return commit_advise(record);
        case VmaOperationKind::Resize:
            return commit_resize(record);
        case VmaOperationKind::Remap:
            return commit_remap(record);
    }

    return RuntimeResult{RuntimeStatus::Failed, "unknown vma operation"};
}

// 回滚权限变更
RuntimeResult rollback_protect(VmaOperationRecord* record) {
    PageProtection protection;
    auto result = protection.set(record->original_protection);
    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::RollbackFailed;
        return result;
    }

    capture_after(record->original_protection.start, &record->after);
    record->status = VmaOperationStatus::RolledBack;
    return RuntimeResult{};
}

// 回滚匿名 VMA 名称
RuntimeResult rollback_anon_name(VmaOperationRecord* record) {
    VmaAttributes attributes;
    std::string original_name;
    RuntimeResult result;
    if (is_anon_name(record->before.path, &original_name) && !original_name.empty()) {
        result = attributes.set_anon_name(record->old_address, record->old_size, original_name);
    } else {
        result = attributes.clear_anon_name(record->old_address, record->old_size);
    }

    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::RollbackFailed;
        return result;
    }

    capture_after(record->old_address, &record->after);
    record->status = VmaOperationStatus::RolledBack;
    return RuntimeResult{};
}

// madvise 不具备可靠逆操作，只记录为已回滚
RuntimeResult rollback_advise(VmaOperationRecord* record) {
    record->result = RuntimeResult{};
    record->status = VmaOperationStatus::RolledBack;
    return RuntimeResult{};
}

// 回滚 mremap 尺寸调整，地址未恢复到原位时视为失败
RuntimeResult rollback_resize(VmaOperationRecord* record) {
    VmaAttributes attributes;
    std::uintptr_t restored = 0;
    auto result = attributes.resize(
        record->new_address,
        record->new_size,
        record->old_size,
        MREMAP_MAYMOVE,
        &restored
    );
    if (result.ok() && restored != record->old_address) {
        record->new_address = restored;
        record->new_size = record->old_size;
        capture_after(restored, &record->after);
        result = RuntimeResult{RuntimeStatus::Failed, "mremap rollback restored a different address"};
    }

    record->result = result;
    if (!result.ok()) {
        record->status = VmaOperationStatus::RollbackFailed;
        return result;
    }

    capture_after(restored, &record->after);
    record->status = VmaOperationStatus::RolledBack;
    return RuntimeResult{};
}

// 回滚 remap：重新占回旧地址、复制内容、恢复权限和匿名名
RuntimeResult rollback_remap(VmaOperationRecord* record) {
    if (record->old_address == 0 || record->new_address == 0 || record->old_size == 0) {
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = RuntimeResult{RuntimeStatus::InvalidArgument, "remap rollback record is incomplete"};
        return record->result;
    }

    MemoryMapEntry existing_old;
    if (capture_after(record->old_address, &existing_old).ok()) {
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = RuntimeResult{RuntimeStatus::Denied, "remap rollback old range is already mapped"};
        return record->result;
    }

    // 旧地址已空时才允许从新地址恢复内容
    MemoryMapEntry current_new;
    auto found_new = capture_after(record->new_address, &current_new);
    if (!found_new.ok()) {
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = found_new;
        return found_new;
    }

    // 源区域不可读时临时放开读写，保证内容能复制回来
    if (!current_new.readable()) {
        if (::mprotect(reinterpret_cast<void*>(record->new_address), record->old_size, PROT_READ | PROT_WRITE) != 0) {
            record->status = VmaOperationStatus::RollbackFailed;
            record->result = RuntimeResult{RuntimeStatus::Failed, errno_detail("remap rollback source mprotect failed")};
            return record->result;
        }
    }

#ifdef MAP_FIXED_NOREPLACE
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;
#else
    record->status = VmaOperationStatus::RollbackFailed;
    record->result = RuntimeResult{RuntimeStatus::Unavailable, "remap rollback requires MAP_FIXED_NOREPLACE"};
    return record->result;
#endif
    // MAP_FIXED_NOREPLACE 避免覆盖已经重新分配的旧地址
    void* restored = ::mmap(
        reinterpret_cast<void*>(record->old_address),
        record->old_size,
        PROT_READ | PROT_WRITE,
        flags,
        -1,
        0
    );
    if (restored == MAP_FAILED || reinterpret_cast<std::uintptr_t>(restored) != record->old_address) {
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = RuntimeResult{RuntimeStatus::Failed, errno_detail("remap rollback mmap failed")};
        return record->result;
    }

    std::memcpy(restored, reinterpret_cast<const void*>(record->new_address), record->old_size);

    // 恢复旧区域原有权限
    PageProtection protection;
    auto protect = protection.set(PageProtectRequest{
        record->old_address,
        record->old_address + static_cast<std::uintptr_t>(record->old_size),
        record->before.readable(),
        record->before.writable(),
        record->before.executable()
    });
    if (!protect.ok()) {
        ::munmap(restored, record->old_size);
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = protect;
        return protect;
    }

    // 尽量恢复旧匿名名，不支持命名的平台允许继续回滚
    std::string original_name;
    if (is_anon_name(record->before.path, &original_name) && !original_name.empty()) {
        VmaAttributes attributes;
        auto named = attributes.set_anon_name(record->old_address, record->old_size, original_name);
        if (!named.ok() && named.status != RuntimeStatus::Unavailable) {
            ::munmap(restored, record->old_size);
            record->status = VmaOperationStatus::RollbackFailed;
            record->result = named;
            return named;
        }
    }

    // 旧地址恢复成功后释放重映射出来的新地址
    if (::munmap(reinterpret_cast<void*>(record->new_address), record->old_size) != 0) {
        record->status = VmaOperationStatus::RollbackFailed;
        record->result = RuntimeResult{RuntimeStatus::Failed, errno_detail("remap rollback new munmap failed")};
        return record->result;
    }

    capture_after(record->old_address, &record->after);
    record->result = RuntimeResult{};
    record->status = VmaOperationStatus::RolledBack;
    return RuntimeResult{};
}

// 按操作类型分发回滚逻辑
RuntimeResult rollback_record(VmaOperationRecord* record) {
    switch (record->kind) {
        case VmaOperationKind::Protect:
            return rollback_protect(record);
        case VmaOperationKind::SetName:
            return rollback_anon_name(record);
        case VmaOperationKind::Advise:
            return rollback_advise(record);
        case VmaOperationKind::Resize:
            return rollback_resize(record);
        case VmaOperationKind::Remap:
            return rollback_remap(record);
    }

    return RuntimeResult{RuntimeStatus::Failed, "unknown vma rollback operation"};
}

// 逆序回滚已应用的操作
RuntimeResult rollback_records(
    std::vector<VmaOperationRecord>* source,
    std::vector<VmaOperationRecord>* out
) {
    if (source == nullptr || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma rollback records"};
    }
    out->clear();

    RuntimeResult final_result;
    for (auto it = source->rbegin(); it != source->rend(); ++it) {
        if (it->status != VmaOperationStatus::Applied) {
            continue;
        }

        auto result = rollback_record(&(*it));
        out->push_back(*it);
        if (!result.ok() && final_result.ok()) {
            final_result = result;
        }
    }

    save_history(*source);
    return final_result;
}

// 创建一条基础操作记录
VmaOperationRecord base_record(std::uint64_t transaction_id, VmaOperationKind kind) {
    VmaOperationRecord record;
    record.id = g_operation_id.fetch_add(1, std::memory_order_relaxed) + 1;
    record.transaction_id = transaction_id;
    record.kind = kind;
    record.status = VmaOperationStatus::Planned;
    return record;
}

} // namespace

// 创建新的 VMA 事务
VmaTransaction::VmaTransaction()
    : id_(g_transaction_id.fetch_add(1, std::memory_order_relaxed) + 1) {}

// 获取事务 ID
std::uint64_t VmaTransaction::id() const {
    return id_;
}

// 添加权限变更操作
RuntimeResult VmaTransaction::add_protect(const PageProtectRequest& request) {
    if (request.start == 0 || request.end <= request.start) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid page protection range"};
    }

    auto record = base_record(id_, VmaOperationKind::Protect);
    record.applied_protection = request;
    records_.push_back(std::move(record));
    return RuntimeResult{};
}

// 添加匿名 VMA 命名操作
RuntimeResult VmaTransaction::add_anon_name(std::uintptr_t start, std::size_t size, const std::string& name) {
    if (start == 0 || size == 0 || name.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid anonymous vma name request"};
    }

    auto record = base_record(id_, VmaOperationKind::SetName);
    record.old_address = start;
    record.old_size = size;
    record.anon_name = name;
    records_.push_back(std::move(record));
    return RuntimeResult{};
}

// 添加 madvise 操作
RuntimeResult VmaTransaction::add_advise(std::uintptr_t start, std::size_t size, int advice) {
    if (start == 0 || size == 0 || advice < 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid madvise request"};
    }

    auto record = base_record(id_, VmaOperationKind::Advise);
    record.old_address = start;
    record.old_size = size;
    record.advice = advice;
    records_.push_back(std::move(record));
    return RuntimeResult{};
}

// 添加 mremap 尺寸调整操作
RuntimeResult VmaTransaction::add_resize(
    std::uintptr_t start,
    std::size_t old_size,
    std::size_t new_size,
    int flags
) {
    if (start == 0 || old_size == 0 || new_size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid mremap request"};
    }

    auto record = base_record(id_, VmaOperationKind::Resize);
    record.old_address = start;
    record.old_size = old_size;
    record.new_size = new_size;
    record.mremap_flags = flags;
    records_.push_back(std::move(record));
    return RuntimeResult{};
}

// 添加 VMA 重映射操作
RuntimeResult VmaTransaction::add_remap(const RemapRequest& request) {
    if (request.start == 0 || request.size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid remap request"};
    }

    auto record = base_record(id_, VmaOperationKind::Remap);
    record.old_address = request.start;
    record.old_size = request.size;
    record.new_size = request.size;
    record.remap_request = request;
    records_.push_back(std::move(record));
    return RuntimeResult{};
}

// 顺序提交事务中的操作
RuntimeResult VmaTransaction::commit(std::vector<VmaOperationRecord>* records) {
    if (records == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma operation records"};
    }
    records->clear();

    RuntimeResult final_result;
    for (auto& record : records_) {
        auto result = commit_record(&record);
        if (!result.ok()) {
            final_result = result;
            break;
        }
    }

    *records = records_;
    save_history(records_);
    return final_result;
}

// 回滚当前事务记录
RuntimeResult VmaTransaction::rollback(std::vector<VmaOperationRecord>* records) {
    return rollback_records(&records_, records);
}

// 按事务 ID 回滚历史中的已提交事务
RuntimeResult VmaTransaction::rollback_committed(
    std::uint64_t transaction_id,
    std::vector<VmaOperationRecord>* records
) {
    if (transaction_id == 0 || records == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma transaction id or rollback records"};
    }

    std::vector<VmaOperationRecord> transaction_records;
    {
        std::lock_guard<std::mutex> lock(g_history_mutex);
        auto found = g_history.find(transaction_id);
        if (found == g_history.end()) {
            records->clear();
            return RuntimeResult{RuntimeStatus::NotFound, "vma transaction was not found"};
        }
        transaction_records = found->second;
    }

    return rollback_records(&transaction_records, records);
}

} // namespace memory
} // namespace runtime
} // namespace nyx
