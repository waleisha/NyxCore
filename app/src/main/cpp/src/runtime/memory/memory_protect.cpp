#include "src/runtime/memory/memory_protect.h"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include <limits>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 按页大小向下对齐
std::uintptr_t align_down(std::uintptr_t value, std::uintptr_t alignment) {
    return value - (value % alignment);
}

// 按页大小向上对齐
std::uintptr_t align_up(std::uintptr_t value, std::uintptr_t alignment) {
    const std::uintptr_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }

    return value + (alignment - remainder);
}

// 转换为 mprotect 权限位
int prot_from(const PageProtectRequest& request) {
    int prot = 0;
    if (request.read) {
        prot |= PROT_READ;
    }
    if (request.write) {
        prot |= PROT_WRITE;
    }
    if (request.execute) {
        prot |= PROT_EXEC;
    }
    return prot;
}

// 格式化 errno 详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "page protection failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

} // namespace

// 设置指定范围页权限
RuntimeResult PageProtection::set(const PageProtectRequest& request) const {
    if (request.start == 0 || request.end <= request.start) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid page protection range"};
    }

    const long page_size_raw = ::sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        return RuntimeResult{RuntimeStatus::Unavailable, "page size is unavailable"};
    }

    const auto page_size = static_cast<std::uintptr_t>(page_size_raw);
    const auto start = align_down(request.start, page_size);
    const auto end = align_up(request.end, page_size);
    if (end <= start || end - start > static_cast<std::uintptr_t>(std::numeric_limits<std::size_t>::max())) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "page protection range is too large"};
    }

    if (::mprotect(reinterpret_cast<void*>(start), static_cast<std::size_t>(end - start), prot_from(request)) != 0) {
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("mprotect failed")};
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
