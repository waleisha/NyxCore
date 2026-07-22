#include "src/runtime/memory/memory_attributes.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif

#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif

namespace nyx {
namespace runtime {
namespace memory {

namespace detail {

namespace {

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

} // namespace

// 查找完全包含指定范围的 VMA
RuntimeResult find_containing_range(std::uintptr_t start, std::uintptr_t end, MemoryMapEntry* out) {
    if (start == 0 || end <= start || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid vma range"};
    }

    MemoryMap memory_map;
    MemoryMapEntry entry;
    auto result = memory_map.find_address(start, &entry);
    if (!result.ok()) {
        return result;
    }
    if (entry.end < end) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "vma range spans multiple mappings"};
    }

    *out = entry;
    return RuntimeResult{};
}

// 按起始地址和大小查找完全包含的 VMA
RuntimeResult find_containing_size(std::uintptr_t start, std::size_t size, MemoryMapEntry* out) {
    std::uintptr_t end = 0;
    if (!checked_end(start, size, &end)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid vma range"};
    }
    return find_containing_range(start, end, out);
}

} // namespace detail

namespace {

// 格式化 errno 详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "vma operation failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

// 判断路径是否是匿名映射名称
bool is_anon_path(const std::string& path) {
    return path.empty() || path.rfind("[anon:", 0) == 0;
}

// 调用 prctl 设置匿名 VMA 名称
RuntimeResult apply_anon_name(std::uintptr_t start, std::size_t size, const char* name) {
    errno = 0;
    if (::prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, start, size, name) != 0) {
        if (errno == EINVAL || errno == ENOSYS) {
            return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("anonymous vma naming is unavailable")};
        }
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("anonymous vma naming failed")};
    }

    return RuntimeResult{};
}

// 调用 mremap syscall
void* call_mremap(std::uintptr_t start, std::size_t old_size, std::size_t new_size, int flags) {
#if defined(__NR_mremap)
    const long result = ::syscall(
        __NR_mremap,
        reinterpret_cast<void*>(start),
        old_size,
        new_size,
        flags
    );
    if (result == -1) {
        return MAP_FAILED;
    }
    return reinterpret_cast<void*>(result);
#else
    errno = ENOSYS;
    return MAP_FAILED;
#endif
}

} // namespace

// 设置匿名 VMA 名称
RuntimeResult VmaAttributes::set_anon_name(
    std::uintptr_t start,
    std::size_t size,
    const std::string& name
) const {
    if (name.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing anonymous vma name"};
    }

    MemoryMapEntry entry;
    auto found = detail::find_containing_size(start, size, &entry);
    if (!found.ok()) {
        return found;
    }
    if (!entry.private_map() || !is_anon_path(entry.path)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "anonymous vma naming requires an anonymous private mapping"};
    }

    return apply_anon_name(start, size, name.c_str());
}

// 清除匿名 VMA 名称
RuntimeResult VmaAttributes::clear_anon_name(std::uintptr_t start, std::size_t size) const {
    MemoryMapEntry entry;
    auto found = detail::find_containing_size(start, size, &entry);
    if (!found.ok()) {
        return found;
    }
    if (!entry.private_map() || !is_anon_path(entry.path)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "anonymous vma naming requires an anonymous private mapping"};
    }

    return apply_anon_name(start, size, nullptr);
}

// 对 VMA 调用 madvise
RuntimeResult VmaAttributes::advise(std::uintptr_t start, std::size_t size, int advice) const {
    if (advice < 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid madvise advice"};
    }

    MemoryMapEntry entry;
    auto found = detail::find_containing_size(start, size, &entry);
    if (!found.ok()) {
        return found;
    }

    errno = 0;
    if (::madvise(reinterpret_cast<void*>(start), size, advice) != 0) {
        if (errno == EINVAL) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, errno_detail("madvise rejected the request")};
        }
        if (errno == ENOSYS) {
            return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("madvise is unavailable")};
        }
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("madvise failed")};
    }

    return RuntimeResult{};
}

// 调整 VMA 大小
RuntimeResult VmaAttributes::resize(
    std::uintptr_t start,
    std::size_t old_size,
    std::size_t new_size,
    int flags,
    std::uintptr_t* out_new_address
) const {
    if (out_new_address == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing mremap output address"};
    }
    *out_new_address = 0;
    if ((flags & MREMAP_FIXED) != 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "mremap fixed target requires an explicit address"};
    }

    MemoryMapEntry entry;
    auto found = detail::find_containing_size(start, old_size, &entry);
    if (!found.ok()) {
        return found;
    }

    errno = 0;
    void* remapped = call_mremap(start, old_size, new_size, flags);
    if (remapped == MAP_FAILED) {
        if (errno == ENOSYS) {
            return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("mremap is unavailable")};
        }
        if (errno == EINVAL) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, errno_detail("mremap rejected the request")};
        }
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("mremap failed")};
    }

    *out_new_address = reinterpret_cast<std::uintptr_t>(remapped);
    return RuntimeResult{};
}

// 用新匿名映射替换原 VMA
RuntimeResult VmaAttributes::remap(const RemapRequest& request, std::uintptr_t* out_new_address) const {
    if (out_new_address == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing remap output address"};
    }
    *out_new_address = 0;

    MemoryMapEntry entry;
    auto found = detail::find_containing_size(request.start, request.size, &entry);
    if (!found.ok()) {
        return found;
    }
    if (request.preserve_content && !entry.readable()) {
        return RuntimeResult{RuntimeStatus::Denied, "remap cannot preserve unreadable content"};
    }

    void* mapped = ::mmap(
        nullptr,
        request.size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );
    if (mapped == MAP_FAILED) {
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("remap mmap failed")};
    }

    const auto new_address = reinterpret_cast<std::uintptr_t>(mapped);
    if (request.preserve_content) {
        std::memcpy(mapped, reinterpret_cast<const void*>(request.start), request.size);
    }

    PageProtection protection;
    auto protect = protection.set(PageProtectRequest{
        new_address,
        new_address + static_cast<std::uintptr_t>(request.size),
        request.permission.read,
        request.permission.write,
        request.permission.execute
    });
    if (!protect.ok()) {
        ::munmap(mapped, request.size);
        return protect;
    }

    if (!request.anon_name.empty()) {
        auto named = apply_anon_name(new_address, request.size, request.anon_name.c_str());
        if (!named.ok() && named.status != RuntimeStatus::Unavailable) {
            ::munmap(mapped, request.size);
            return named;
        }
    }

    if (::munmap(reinterpret_cast<void*>(request.start), request.size) != 0) {
        const RuntimeResult result{RuntimeStatus::Failed, errno_detail("remap old munmap failed")};
        ::munmap(mapped, request.size);
        return result;
    }

    *out_new_address = new_address;
    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
