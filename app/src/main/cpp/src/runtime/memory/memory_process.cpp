#include "src/runtime/memory/memory_process.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 构造 /proc/<pid>/<leaf> 路径
std::string proc_path(pid_t pid, const char* leaf) {
    if (pid <= 0 || leaf == nullptr || leaf[0] == '\0') {
        return {};
    }

    return "/proc/" + std::to_string(static_cast<int>(pid)) + "/" + leaf;
}

// 判断文本是否全为数字
bool all_digits(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; ++p) {
        if (!std::isdigit(*p)) {
            return false;
        }
    }
    return true;
}

// 读取进程 cmdline 的首段
std::string read_cmdline(pid_t pid) {
    std::ifstream input(proc_path(pid, "cmdline"), std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::string text;
    std::getline(input, text, '\0');
    return text;
}

// 格式化 errno 详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "process lookup failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

} // namespace

// 当前进程
MemProcess MemProcess::current() {
    MemProcess process;
    process.pid = ::getpid();
    process.self = true;
    process.maps_path = "/proc/self/maps";
    process.mem_path = "/proc/self/mem";
    return process;
}

// 从 pid 构造进程描述
MemProcess MemProcess::from_pid(pid_t pid) {
    MemProcess process;
    process.pid = pid;
    process.self = pid == ::getpid();
    process.maps_path = process.self ? "/proc/self/maps" : proc_path(pid, "maps");
    process.mem_path = process.self ? "/proc/self/mem" : proc_path(pid, "mem");
    return process;
}

// 按包名查找 pid
RuntimeResult find_pid(const std::string& package_name, pid_t* out) {
    if (package_name.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing package name or pid output"};
    }
    *out = 0;

    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) {
        return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("failed to open /proc")};
    }

    RuntimeResult result{RuntimeStatus::NotFound, "package process was not found"};
    while (dirent* entry = ::readdir(proc)) {
        if (!all_digits(entry->d_name)) {
            continue;
        }

        const auto pid = static_cast<pid_t>(std::atoi(entry->d_name));
        const std::string cmdline = read_cmdline(pid);
        if (cmdline == package_name) {
            *out = pid;
            result = RuntimeResult{};
            break;
        }
    }

    ::closedir(proc);
    return result;
}

// 按包名构造进程描述
RuntimeResult process_from_package(const std::string& package_name, MemProcess* out) {
    if (package_name.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing package name or process output"};
    }

    pid_t pid = 0;
    auto found = find_pid(package_name, &pid);
    if (!found.ok()) {
        *out = MemProcess{};
        return found;
    }

    *out = MemProcess::from_pid(pid);
    out->package_name = package_name;
    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
