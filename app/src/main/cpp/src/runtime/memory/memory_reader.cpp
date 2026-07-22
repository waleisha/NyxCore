#include "src/runtime/memory/memory_reader.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <utility>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 格式化 errno 详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "memory read failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

} // namespace

// 创建指定进程的读取器
MemoryReader::MemoryReader(MemProcess process)
    : process_(std::move(process)) {}

// 关闭缓存的 mem fd
MemoryReader::~MemoryReader() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

// 移动读取器并转移 fd
MemoryReader::MemoryReader(MemoryReader&& other) noexcept
    : process_(std::move(other.process_))
    , fd_(other.fd_) {
    other.fd_ = -1;
}

// 移动赋值并转移 fd
MemoryReader& MemoryReader::operator=(MemoryReader&& other) noexcept {
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

// 从目标地址读取指定大小数据
RuntimeResult MemoryReader::read(std::uintptr_t addr, void* out, std::size_t size) const {
    if (addr == 0 || out == nullptr || size == 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory read range"};
    }

    if (process_.self) {
        std::memcpy(out, reinterpret_cast<const void*>(addr), size);
        return RuntimeResult{};
    }

    if (fd_ < 0) {
        fd_ = ::open(process_.mem_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0) {
            return RuntimeResult{RuntimeStatus::Denied, errno_detail(("failed to open " + process_.mem_path).c_str())};
        }
    }

    const ssize_t read_size = ::pread64(fd_, out, size, static_cast<off64_t>(addr));
    if (read_size < 0) {
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("pread64 failed")};
    }
    if (static_cast<std::size_t>(read_size) != size) {
        return RuntimeResult{RuntimeStatus::Failed, "memory read returned a partial buffer"};
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
