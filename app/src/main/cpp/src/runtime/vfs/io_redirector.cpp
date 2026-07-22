#include "src/runtime/vfs/io_redirector.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nyx {
namespace runtime {
namespace vfs {

namespace {

// errno 转 runtime 状态
RuntimeStatus io_status(int error) {
    if (error == ENOENT) {
        return RuntimeStatus::NotFound;
    }
    if (error == EACCES || error == EPERM) {
        return RuntimeStatus::Denied;
    }
    if (error == EINVAL) {
        return RuntimeStatus::InvalidArgument;
    }
    return RuntimeStatus::Failed;
}

// 拼接 I/O 失败详情
std::string io_detail(const char* op, int error, const PathDecision& decision) {
    std::string detail = op != nullptr ? op : "io";
    detail += " failed";
    detail += ": ";
    detail += std::strerror(error);
    detail += "; path=";
    detail += decision.path;
    detail += "; target=";
    detail += decision.target;
    detail += "; reason=";
    detail += decision.reason;
    return detail;
}

// 填充打开句柄
void fill_handle(const PathDecision& decision, int fd, OpenHandle* out) {
    out->fd = fd;
    out->path = decision.path;
    out->target = decision.target;
    out->redirected = decision.redirected;
}

// 统一处理 stat 系列结果
RuntimeResult stat_result(const char* op, int result, const PathDecision& decision) {
    if (result == 0) {
        return RuntimeResult{};
    }

    const int error = errno;
    return RuntimeResult{io_status(error), io_detail(op, error, decision)};
}

} // namespace

// 使用路径映射表创建重定向器
IoRedirector::IoRedirector(const PathMapper& mapper) : mapper_(mapper) {}

// 解析路径重定向决策
RuntimeResult IoRedirector::resolve(const std::string& path, int flags, PathDecision* out) const {
    if (path.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing path or output decision"};
    }

    PathDecision decision;
    decision.path = path;
    decision.target = path;
    decision.reason = "no path rule";

    const auto policy = mapper_.policy();
    std::string reason;
    if (!policy.allows_open(path, flags, &reason)) {
        return RuntimeResult{RuntimeStatus::Denied, reason};
    }

    std::string mapped;
    auto map_result = mapper_.map(path, &mapped);
    if (map_result.ok()) {
        // 映射后的目标也要重新经过目标策略和打开策略校验
        if (!policy.allows(path, mapped, &reason) || !policy.allows_open(mapped, flags, &reason)) {
            return RuntimeResult{RuntimeStatus::Denied, reason};
        }

        decision.target = mapped;
        decision.reason = "mapped by path rule";
        decision.redirected = true;
    } else if (map_result.status != RuntimeStatus::NotFound) {
        return map_result;
    }

    *out = decision;
    return RuntimeResult{};
}

// 打开路径
RuntimeResult IoRedirector::open(const OpenRequest& request, OpenHandle* out) const {
    return open_at(OpenAtRequest{kCurrentDirectoryFd, request.path, request.flags, request.mode}, out);
}

// 以 openat 语义打开路径
RuntimeResult IoRedirector::open_at(const OpenAtRequest& request, OpenHandle* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing output handle"};
    }

    PathDecision decision;
    auto resolve_result = resolve(request.path, request.flags, &decision);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    errno = 0;
    int fd = -1;
    // 重定向目标是绝对策略产物，不再套用调用方 dir_fd
    if (decision.redirected) {
        fd = ::open(decision.target.c_str(), request.flags, static_cast<mode_t>(request.mode));
    } else {
        fd = ::openat(request.dir_fd, decision.target.c_str(), request.flags, static_cast<mode_t>(request.mode));
    }
    if (fd < 0) {
        const int error = errno;
        return RuntimeResult{io_status(error), io_detail("open", error, decision)};
    }

    fill_handle(decision, fd, out);
    return RuntimeResult{};
}

// stat 路径
RuntimeResult IoRedirector::stat_path(const StatRequest& request, struct stat* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stat output"};
    }

    PathDecision decision;
    auto resolve_result = resolve(request.path, 0, &decision);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    errno = 0;
    return stat_result("stat", ::fstatat(kCurrentDirectoryFd, decision.target.c_str(), out, request.flags), decision);
}

// lstat 路径
RuntimeResult IoRedirector::lstat_path(const StatRequest& request, struct stat* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stat output"};
    }

    PathDecision decision;
    auto resolve_result = resolve(request.path, 0, &decision);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    errno = 0;
    return stat_result("lstat", ::fstatat(kCurrentDirectoryFd, decision.target.c_str(), out, request.flags | AT_SYMLINK_NOFOLLOW), decision);
}

// fstatat 路径
RuntimeResult IoRedirector::fstat_at(const StatAtRequest& request, struct stat* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing stat output"};
    }

    PathDecision decision;
    auto resolve_result = resolve(request.path, 0, &decision);
    if (!resolve_result.ok()) {
        return resolve_result;
    }

    errno = 0;
    // 重定向后目标路径不再相对调用方 dir_fd
    const int dir_fd = decision.redirected ? kCurrentDirectoryFd : request.dir_fd;
    return stat_result("fstatat", ::fstatat(dir_fd, decision.target.c_str(), out, request.flags), decision);
}

// 关闭打开句柄
RuntimeResult IoRedirector::close(OpenHandle* handle) const {
    if (handle == nullptr || !handle->valid()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing open file handle"};
    }

    if (::close(handle->fd) != 0) {
        return RuntimeResult{io_status(errno), "close failed"};
    }

    handle->fd = -1;
    handle->path.clear();
    handle->target.clear();
    handle->redirected = false;
    return RuntimeResult{};
}

} // namespace vfs
} // namespace runtime
} // namespace nyx
