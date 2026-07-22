#pragma once

#include <cstdint>
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 页权限修改请求
struct PageProtectRequest {
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址，左闭右开
    std::uintptr_t end = 0;
    // 是否可读
    bool read = true;
    // 是否可写
    bool write = false;
    // 是否可执行
    bool execute = false;
};

// 页保护操作封装
class PageProtection {
public:
    // 设置指定范围页权限
    RuntimeResult set(const PageProtectRequest& request) const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
