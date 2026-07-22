#include "src/runtime/memory/memory_system.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/wait.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 拼接 errno 失败详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "system operation failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
}

// 为 su -c 等 shell 命令包裹单引号参数
std::string quote_shell(const std::string& text) {
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

// 判断 /proc 项是否为纯数字 pid
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

// 拼接进程 procfs 子路径
std::string proc_path(pid_t pid, const char* leaf) {
    return "/proc/" + std::to_string(static_cast<int>(pid)) + "/" + leaf;
}

// 读取进程命令行首段
std::string read_cmdline(pid_t pid) {
    std::ifstream input(proc_path(pid, "cmdline"), std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::string text;
    std::getline(input, text, '\0');
    return text;
}

// 向目标进程发送信号
RuntimeResult signal_process(pid_t pid, int signal, const char* action) {
    if (pid <= 0) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing process id"};
    }
    if (::kill(pid, signal) != 0) {
        const RuntimeStatus status = errno == ESRCH ? RuntimeStatus::NotFound : RuntimeStatus::Denied;
        return RuntimeResult{status, errno_detail(action)};
    }
    return RuntimeResult{};
}

// 执行系统命令并收集合并输出
RuntimeResult exec_command(const std::string& cmd, std::string* output) {
    if (cmd.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing system command"};
    }
    if (output != nullptr) {
        output->clear();
    }

    const std::string shell_cmd = cmd + " 2>&1";
    FILE* pipe = ::popen(shell_cmd.c_str(), "r");
    if (pipe == nullptr) {
        return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("popen failed")};
    }

    std::array<char, 256> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        if (output != nullptr) {
            output->append(buffer.data());
        }
    }

    const int status = ::pclose(pipe);
    if (status == -1) {
        return RuntimeResult{RuntimeStatus::Failed, errno_detail("pclose failed")};
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::ostringstream detail;
        detail << "system command failed";
        if (WIFEXITED(status)) {
            detail << " with exit code " << WEXITSTATUS(status);
        }
        return RuntimeResult{RuntimeStatus::Failed, detail.str()};
    }

    return RuntimeResult{};
}

} // namespace

// 执行普通 shell 命令
RuntimeResult SystemControl::exec(const std::string& cmd, std::string* output) {
    return exec_command(cmd, output);
}

// 通过 su 执行 root 命令
RuntimeResult SystemControl::exec_root(const std::string& cmd, std::string* output) {
    if (cmd.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing root system command"};
    }

    const RuntimeResult result = exec_command("su -c " + quote_shell(cmd), output);
    if (!result.ok()) {
        return RuntimeResult{RuntimeStatus::Denied, result.detail};
    }
    return result;
}

// 重启设备
RuntimeResult SystemControl::reboot() {
    return exec_root("reboot", nullptr);
}

// 暂停进程
RuntimeResult ProcessControl::stop(pid_t pid) {
    return signal_process(pid, SIGSTOP, "failed to stop process");
}

// 恢复进程
RuntimeResult ProcessControl::resume(pid_t pid) {
    return signal_process(pid, SIGCONT, "failed to resume process");
}

// 终止进程
RuntimeResult ProcessControl::terminate(pid_t pid) {
    return signal_process(pid, SIGTERM, "failed to terminate process");
}

// 按 cmdline 片段终止匹配进程
RuntimeResult ProcessControl::terminate_by_pattern(const std::string& pattern) {
    if (pattern.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing process pattern"};
    }

    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) {
        return RuntimeResult{RuntimeStatus::Unavailable, errno_detail("failed to open /proc")};
    }

    std::size_t matched = 0;
    std::size_t failed = 0;
    while (dirent* entry = ::readdir(proc)) {
        if (!all_digits(entry->d_name)) {
            continue;
        }
        const auto pid = static_cast<pid_t>(std::atoi(entry->d_name));
        const std::string cmdline = read_cmdline(pid);
        if (cmdline.find(pattern) == std::string::npos) {
            continue;
        }

        ++matched;
        if (!terminate(pid).ok()) {
            ++failed;
        }
    }
    ::closedir(proc);

    if (matched == 0) {
        return RuntimeResult{RuntimeStatus::NotFound, "process pattern was not found"};
    }
    if (failed != 0) {
        std::ostringstream detail;
        detail << "failed to terminate " << failed << " of " << matched << " matching processes";
        return RuntimeResult{RuntimeStatus::Failed, detail.str()};
    }
    return RuntimeResult{};
}

// 安装 APK 包
RuntimeResult PackageManager::install(const std::string& apk_path) {
    if (apk_path.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing apk path"};
    }
    return SystemControl::exec_root("pm install -r " + quote_shell(apk_path), nullptr);
}

// 卸载应用包
RuntimeResult PackageManager::uninstall(const std::string& package_name) {
    if (package_name.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing package name"};
    }
    return SystemControl::exec_root("pm uninstall " + quote_shell(package_name), nullptr);
}

} // namespace memory
} // namespace runtime
} // namespace nyx
