#pragma once

#include <cstddef>
#include <cstdint>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 内存写入策略
enum class WriteMode {
    // 只在当前权限允许时写入
    CurrentPermission,
    // 自动临时改为可写后恢复
    AutoProtect,
    // 自动改权限并校验恢复结果
    SecureWrite,
};

// 进程内存写入器
class MemoryWriter {
public:
    // 创建指定进程的写入器
    explicit MemoryWriter(MemProcess process);
    ~MemoryWriter();

    // 写入器持有 fd，禁止复制
    MemoryWriter(const MemoryWriter&) = delete;
    MemoryWriter& operator=(const MemoryWriter&) = delete;
    // 允许移动写入器
    MemoryWriter(MemoryWriter&& other) noexcept;
    MemoryWriter& operator=(MemoryWriter&& other) noexcept;

    // 写入目标地址
    RuntimeResult write(std::uintptr_t addr, const void* data, std::size_t size, WriteMode mode) const;

private:
    // 目标进程
    MemProcess process_;
    // 缓存的 /proc/<pid>/mem fd
    mutable int fd_ = -1;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
