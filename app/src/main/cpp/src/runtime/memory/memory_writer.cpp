#include "src/runtime/memory/memory_writer.h"

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/memory/memory_protect.h"

#include <cerrno>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

#include <string>
#include <utility>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 格式化 errno 详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "memory write failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

// 检查 start + size 是否溢出
bool checked_end(std::uintptr_t start, std::size_t size, std::uintptr_t* out) {
    if (out == nullptr || start == 0 || size == 0) {
        return false;
    }
    if (size > static_cast<std::size_t>(std::numeric_limits<std::uintptr_t>::max() - start)) {
        return false;
    }

    *out = start + static_cast<std::uintptr_t>(size);
    return *out > start;
}

// 查找完全包含写入范围的 VMA
RuntimeResult find_range(const MemProcess& process, std::uintptr_t start, std::size_t size, MemoryMapEntry* out) {
    std::uintptr_t end = 0;
    if (!checked_end(start, size, &end) || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory write range"};
    }

    MemoryMap map(process);
    auto found = map.find_address(start, out);
    if (!found.ok()) {
        return found;
    }
    if (end > out->end) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "memory write range crosses a VMA boundary"};
    }

    return RuntimeResult{};
}

// 通过 /proc/<pid>/mem 写入远程进程
RuntimeResult write_pid(const MemProcess& process, std::uintptr_t addr, const void* data, std::size_t size, int& cached_fd) {
    if (cached_fd < 0) {
        cached_fd = ::open(process.mem_path.c_str(), O_RDWR | O_CLOEXEC);
        if (cached_fd < 0) {
            return RuntimeResult{RuntimeStatus::Denied, errno_detail(("failed to open " + process.mem_path).c_str())};
        }
    }

    const ssize_t wrote = ::pwrite64(cached_fd, data, size, static_cast<off64_t>(addr));
    if (wrote < 0) {
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("pwrite64 failed")};
    }
    if (static_cast<std::size_t>(wrote) != size) {
        return RuntimeResult{RuntimeStatus::Failed, "memory write returned a partial buffer"};
    }

    return RuntimeResult{};
}

// 判断两个 maps 条目的 RWX 权限是否一致
bool same_protection(const MemoryMapEntry& lhs, const MemoryMapEntry& rhs) {
    return lhs.readable() == rhs.readable() &&
        lhs.writable() == rhs.writable() &&
        lhs.executable() == rhs.executable();
}

} // namespace

// 创建指定进程的写入器
MemoryWriter::MemoryWriter(MemProcess process)
    : process_(std::move(process)) {}

// 关闭缓存的 mem fd
MemoryWriter::~MemoryWriter() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

// 移动写入器并转移 fd
MemoryWriter::MemoryWriter(MemoryWriter&& other) noexcept
    : process_(std::move(other.process_))
    , fd_(other.fd_) {
    other.fd_ = -1;
}

// 移动赋值并转移 fd
MemoryWriter& MemoryWriter::operator=(MemoryWriter&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        process_ = std::move(other.process_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

// 写入目标地址，按策略处理权限
RuntimeResult MemoryWriter::write(std::uintptr_t addr, const void* data, std::size_t size, WriteMode mode) const {
    if (addr == 0 || data == nullptr || size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory write range"};
    }

    MemoryMapEntry entry;
    auto found = find_range(process_, addr, size, &entry);
    if (!found.ok()) {
        return found;
    }

    if (!process_.self) {
        if (mode == WriteMode::SecureWrite) {
            return RuntimeResult{
                RuntimeStatus::Unavailable,
                "secure memory write requires current-process page protection control"
            };
        }
        // Cross-process AutoProtect: /proc/pid/mem pwrite64 bypasses page permissions
        // on most Android kernels, so direct write is sufficient. If strict permission
        // enforcement is needed, use ptrace or process_vm_writev instead.
        return write_pid(process_, addr, data, size, fd_);
    }

    const auto end = addr + static_cast<std::uintptr_t>(size);
    if (mode == WriteMode::CurrentPermission) {
        if (!entry.writable()) {
            return RuntimeResult{RuntimeStatus::Denied, "memory write range is not writable"};
        }

        std::memcpy(reinterpret_cast<void*>(addr), data, size);
        if (entry.executable()) {
            __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(end));
        }
        return RuntimeResult{};
    }

    const bool secure = mode == WriteMode::SecureWrite;
    PageProtection protection;
    auto unlock = protection.set(PageProtectRequest{addr, end, true, true, entry.executable()});
    if (!unlock.ok()) {
        return unlock;
    }

    if (secure) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    std::memcpy(reinterpret_cast<void*>(addr), data, size);
    if (entry.executable()) {
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(end));
    }
    if (secure) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }

    auto restore = protection.set(PageProtectRequest{
        addr,
        end,
        entry.readable(),
        entry.writable(),
        entry.executable()
    });
    if (!restore.ok()) {
        return restore;
    }

    if (secure) {
        MemoryMapEntry restored;
        auto refound = MemoryMap(process_).find_address(addr, &restored);
        if (!refound.ok()) {
            return refound;
        }
        if (!same_protection(entry, restored)) {
            return RuntimeResult{
                RuntimeStatus::Failed,
                "secure memory write did not restore the original page protection"
            };
        }
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
