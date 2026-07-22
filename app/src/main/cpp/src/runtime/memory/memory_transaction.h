#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/memory/memory_protect.h"
#include "src/runtime/memory/memory_attributes.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// VMA 操作类型
enum class VmaOperationKind {
    // 修改页权限
    Protect,
    // 设置匿名映射名称
    SetName,
    // madvise
    Advise,
    // mremap 调整大小
    Resize,
    // 用新匿名映射替换
    Remap,
};

// VMA 操作状态
enum class VmaOperationStatus {
    // 已计划
    Planned,
    // 已应用
    Applied,
    // 已回滚
    RolledBack,
    // 应用失败
    Failed,
    // 回滚失败
    RollbackFailed,
};

// 单条 VMA 操作记录
struct VmaOperationRecord {
    // 操作 ID
    std::uint64_t id = 0;
    // 事务 ID
    std::uint64_t transaction_id = 0;
    // 操作类型
    VmaOperationKind kind = VmaOperationKind::Protect;
    // 操作状态
    VmaOperationStatus status = VmaOperationStatus::Planned;
    // 操作前映射
    MemoryMapEntry before;
    // 操作后映射
    MemoryMapEntry after;
    // 原始页权限
    PageProtectRequest original_protection;
    // 应用的页权限
    PageProtectRequest applied_protection;
    // 匿名映射名称
    std::string anon_name;
    // madvise 参数
    int advice = 0;
    // mremap flags
    int mremap_flags = 0;
    // 原地址
    std::uintptr_t old_address = 0;
    // 新地址
    std::uintptr_t new_address = 0;
    // 原大小
    std::size_t old_size = 0;
    // 新大小
    std::size_t new_size = 0;
    // remap 请求
    RemapRequest remap_request;
    // 操作结果
    RuntimeResult result;
};

// VMA 事务，按记录顺序提交、反序回滚
class VmaTransaction {
public:
    // 创建新事务
    VmaTransaction();

    // 获取事务 ID
    std::uint64_t id() const;

    // 添加页权限操作
    RuntimeResult add_protect(const PageProtectRequest& request);
    // 添加匿名命名操作
    RuntimeResult add_anon_name(std::uintptr_t start, std::size_t size, const std::string& name);
    // 添加 madvise 操作
    RuntimeResult add_advise(std::uintptr_t start, std::size_t size, int advice);
    // 添加 mremap 操作
    RuntimeResult add_resize(std::uintptr_t start, std::size_t old_size, std::size_t new_size, int flags);
    // 添加 remap 操作
    RuntimeResult add_remap(const RemapRequest& request);
    // 提交事务
    RuntimeResult commit(std::vector<VmaOperationRecord>* records);
    // 回滚当前事务中已应用的操作
    RuntimeResult rollback(std::vector<VmaOperationRecord>* records);

    // 按事务 ID 回滚历史记录
    static RuntimeResult rollback_committed(std::uint64_t transaction_id, std::vector<VmaOperationRecord>* records);

private:
    // 事务 ID
    std::uint64_t id_ = 0;
    // 操作记录
    std::vector<VmaOperationRecord> records_;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
