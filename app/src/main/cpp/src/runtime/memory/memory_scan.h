#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/memory/memory_value.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 内存扫描请求
struct ScanRequest {
    // 扫描区域 ID
    int area = 0;
    // 自定义 maps 文本过滤
    std::string custom_area;
    // 搜索条件
    SearchTerm term;
    // 最大结果数，0 表示不限制
    std::size_t max_results = 0;
};

// 内存扫描器
class MemoryScanner {
public:
    // 创建指定进程的扫描器
    explicit MemoryScanner(MemProcess process);

    // 执行单值或范围搜索
    RuntimeResult search(const ScanRequest& request, std::vector<std::uintptr_t>* out) const;
    // 执行联合搜索
    RuntimeResult search_united(const ScanRequest& request, const UnitedSearch& united, std::vector<std::uintptr_t>* out) const;

private:
    // 目标进程
    MemProcess process_;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
