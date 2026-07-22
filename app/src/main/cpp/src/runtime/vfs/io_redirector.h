#pragma once

#include <string>
#include <sys/stat.h>

#include "src/runtime/runtime_result.h"
#include "src/runtime/vfs/path_mapper.h"

namespace nyx {
namespace runtime {
namespace vfs {

// 当前目录 fd，和 AT_FDCWD 保持一致
constexpr int kCurrentDirectoryFd = -100;

// 路径重定向决策
struct PathDecision {
    // 原始路径
    std::string path;
    // 实际访问路径
    std::string target;
    // 决策原因
    std::string reason;
    // 是否发生重定向
    bool redirected = false;
};

// open 请求
struct OpenRequest {
    // 打开路径
    std::string path;
    // open flags
    int flags = 0;
    // 创建文件时使用的 mode
    int mode = 0600;
};

// openat 请求
struct OpenAtRequest {
    // 目录 fd
    int dir_fd = kCurrentDirectoryFd;
    // 打开路径
    std::string path;
    // open flags
    int flags = 0;
    // 创建文件时使用的 mode
    int mode = 0600;
};

// stat 请求
struct StatRequest {
    // 查询路径
    std::string path;
    // fstatat flags
    int flags = 0;
};

// fstatat 请求
struct StatAtRequest {
    // 目录 fd
    int dir_fd = kCurrentDirectoryFd;
    // 查询路径
    std::string path;
    // fstatat flags
    int flags = 0;
};

// 打开的文件句柄
struct OpenHandle {
    // 文件描述符
    int fd = -1;
    // 原始路径
    std::string path;
    // 实际访问路径
    std::string target;
    // 是否发生重定向
    bool redirected = false;

    // 句柄是否有效
    bool valid() const {
        return fd >= 0;
    }
};

// I/O 重定向执行器
class IoRedirector {
public:
    // 使用路径映射表创建重定向器
    explicit IoRedirector(const PathMapper& mapper);

    // 解析路径重定向决策
    RuntimeResult resolve(const std::string& path, int flags, PathDecision* out) const;
    // 打开路径
    RuntimeResult open(const OpenRequest& request, OpenHandle* out) const;
    // 以 openat 语义打开路径
    RuntimeResult open_at(const OpenAtRequest& request, OpenHandle* out) const;
    // stat 路径
    RuntimeResult stat_path(const StatRequest& request, struct stat* out) const;
    // lstat 路径
    RuntimeResult lstat_path(const StatRequest& request, struct stat* out) const;
    // fstatat 路径
    RuntimeResult fstat_at(const StatAtRequest& request, struct stat* out) const;
    // 关闭打开句柄
    RuntimeResult close(OpenHandle* handle) const;

private:
    // 路径映射表引用
    const PathMapper& mapper_;
};

} // namespace vfs
} // namespace runtime
} // namespace nyx
