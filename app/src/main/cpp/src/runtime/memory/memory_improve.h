#pragma once

#include <cstdint>
#include <vector>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/memory/memory_value.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 基于已有搜索结果继续筛选
class MemoryImprover {
public:
    // 创建指定进程的筛选器
    explicit MemoryImprover(MemProcess process);

    // 按 offset 后的单值或范围条件筛选结果
    RuntimeResult filter(
        const std::vector<std::uintptr_t>& input,
        const SearchTerm& term,
        std::intptr_t offset,
        std::vector<std::uintptr_t>* out
    ) const;
    // 按 offset 后的联合条件筛选结果
    RuntimeResult filter_united(
        const std::vector<std::uintptr_t>& input,
        const UnitedSearch& united,
        std::intptr_t offset,
        std::vector<std::uintptr_t>* out
    ) const;

private:
    // 目标进程
    MemProcess process_;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
