#pragma once

#include <cstddef>
#include <cstdint>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 进程内存读取器
class MemoryReader {
public:
    // 创建指定进程的读取器
    explicit MemoryReader(MemProcess process);
    ~MemoryReader();

    // 读取器持有 fd，禁止复制
    MemoryReader(const MemoryReader&) = delete;
    MemoryReader& operator=(const MemoryReader&) = delete;
    // 允许移动读取器
    MemoryReader(MemoryReader&& other) noexcept;
    MemoryReader& operator=(MemoryReader&& other) noexcept;

    // 从目标地址读取指定大小数据
    RuntimeResult read(std::uintptr_t addr, void* out, std::size_t size) const;

private:
    // 目标进程
    MemProcess process_;
    // 缓存的 /proc/<pid>/mem fd
    mutable int fd_ = -1;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
