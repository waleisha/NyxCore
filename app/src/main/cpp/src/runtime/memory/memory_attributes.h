#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/runtime/memory/memory_permission.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// VMA 重映射请求
struct RemapRequest {
    // 原始起始地址
    std::uintptr_t start = 0;
    // 映射大小
    std::size_t size = 0;
    // 新映射权限
    PagePermission permission;
    // 是否复制原内容
    bool preserve_content = true;
    // 新匿名映射名称
    std::string anon_name;
};

// VMA 属性操作封装
class VmaAttributes {
public:
    // 设置匿名 VMA 名称
    RuntimeResult set_anon_name(std::uintptr_t start, std::size_t size, const std::string& name) const;
    // 清除匿名 VMA 名称
    RuntimeResult clear_anon_name(std::uintptr_t start, std::size_t size) const;
    // 对 VMA 调用 madvise
    RuntimeResult advise(std::uintptr_t start, std::size_t size, int advice) const;
    // 调整 VMA 大小
    RuntimeResult resize(
        std::uintptr_t start,
        std::size_t old_size,
        std::size_t new_size,
        int flags,
        std::uintptr_t* out_new_address
    ) const;
    // 用新匿名映射替换原 VMA
    RuntimeResult remap(const RemapRequest& request, std::uintptr_t* out_new_address) const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
