#pragma once

#include <sys/types.h>

#include <string>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 系统命令控制
class SystemControl {
public:
    // 执行普通 shell 命令
    static RuntimeResult exec(const std::string& cmd, std::string* output);
    // 通过 su 执行命令
    static RuntimeResult exec_root(const std::string& cmd, std::string* output);
    // 重启设备
    static RuntimeResult reboot();
};

// 进程控制
class ProcessControl {
public:
    // 暂停进程
    static RuntimeResult stop(pid_t pid);
    // 恢复进程
    static RuntimeResult resume(pid_t pid);
    // 结束进程
    static RuntimeResult terminate(pid_t pid);
    // 按 cmdline 模式结束进程
    static RuntimeResult terminate_by_pattern(const std::string& pattern);
};

// Android 包管理封装
class PackageManager {
public:
    // 安装 APK
    static RuntimeResult install(const std::string& apk_path);
    // 卸载包名
    static RuntimeResult uninstall(const std::string& package_name);
};

} // namespace memory
} // namespace runtime
} // namespace nyx
