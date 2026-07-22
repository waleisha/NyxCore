#pragma once

#include <sys/types.h>

#include <string>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 目标进程描述
struct MemProcess {
    // 进程 ID
    pid_t pid = 0;
    // 是否当前进程
    bool self = true;
    // 包名
    std::string package_name;
    // maps 文件路径
    std::string maps_path;
    // mem 文件路径
    std::string mem_path;

    // 当前进程
    static MemProcess current();
    // 从 pid 构造进程描述
    static MemProcess from_pid(pid_t pid);
};

// 按包名查找 pid
RuntimeResult find_pid(const std::string& package_name, pid_t* out);
// 按包名构造进程描述
RuntimeResult process_from_package(const std::string& package_name, MemProcess* out);

} // namespace memory
} // namespace runtime
} // namespace nyx
