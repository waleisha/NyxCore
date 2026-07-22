#include "src/runtime/vfs/path_policy.h"

#include <fcntl.h>

namespace nyx {
namespace runtime {
namespace vfs {

namespace {

// 默认允许的公共根目录
constexpr const char* kCommonRoots[] = {
    "/sdcard",
    "/storage/emulated/0",
    "/data/local/tmp",
};

// 判断路径中是否包含父目录跳转
bool contains_parent_step(const std::string& path) {
    return path == ".." ||
        path.rfind("../", 0) == 0 ||
        path.find("/../") != std::string::npos ||
        (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0);
}

// 判断路径是否位于指定根目录下
bool starts_with_root(const std::string& path, const std::string& root) {
    if (root.empty() || path.rfind(root, 0) != 0) {
        return false;
    }
    return path.size() == root.size() || root.back() == '/' || path[root.size()] == '/';
}

// 判断 open flags 是否包含写语义
bool has_write_flags(int flags) {
    const int access = flags & O_ACCMODE;
    if (access == O_WRONLY || access == O_RDWR) {
        return true;
    }
    if ((flags & O_CREAT) != 0) {
        return true;
    }
    if ((flags & O_TRUNC) != 0) {
        return true;
    }
    if ((flags & O_APPEND) != 0) {
        return true;
    }
    return false;
}

// 写入策略拒绝原因
void set_reason(std::string* reason, const char* text) {
    if (reason != nullptr) {
        *reason = text;
    }
}

// 判断路径是否位于策略允许的任一根目录下
bool starts_with_any_root(const std::string& path, const PathPolicy& policy) {
    if (starts_with_root(path, policy.private_root)) {
        return true;
    }

    for (const auto& root : policy.allowed_roots) {
        if (starts_with_root(path, root)) {
            return true;
        }
    }

    if (!policy.allow_common_roots) {
        return false;
    }

    for (const char* root : kCommonRoots) {
        if (starts_with_root(path, root)) {
            return true;
        }
    }

    return false;
}

} // namespace

// 判断重定向目标是否符合策略
bool PathPolicy::allows(const std::string& from, const std::string& to, std::string* reason) const {
    if (from.empty() || to.empty()) {
        set_reason(reason, "empty path");
        return false;
    }
    if (contains_parent_step(from) || contains_parent_step(to)) {
        set_reason(reason, "path traversal is not allowed");
        return false;
    }
    if (!starts_with_any_root(to, *this)) {
        set_reason(reason, "target is outside allowed VFS roots");
        return false;
    }

    set_reason(reason, "allowed");
    return true;
}

// 判断打开路径和 flags 是否符合策略
bool PathPolicy::allows_open(const std::string& path, int flags, std::string* reason) const {
    if (path.empty()) {
        set_reason(reason, "empty path");
        return false;
    }
    if (contains_parent_step(path)) {
        set_reason(reason, "path traversal is not allowed");
        return false;
    }
    if (read_only && has_write_flags(flags)) {
        set_reason(reason, "write access denied by read-only policy");
        return false;
    }

    set_reason(reason, "allowed");
    return true;
}

} // namespace vfs
} // namespace runtime
} // namespace nyx
