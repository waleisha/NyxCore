#include "src/runtime/memory/memory_inotify.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 拼接 inotify 失败详情
std::string errno_detail(const char* prefix) {
    std::string detail = prefix != nullptr ? prefix : "inotify operation failed";
    detail += ": ";
    detail += std::strerror(errno);
    return detail;
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

// 判断 fdinfo 是否包含 inotify 监控项
bool fdinfo_has_inotify(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.find("inotify") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 关闭当前进程持有的 inotify fd
std::size_t close_self_inotify_fds() {
    DIR* fdinfo = ::opendir("/proc/self/fdinfo");
    if (fdinfo == nullptr) {
        return 0;
    }

    std::size_t closed = 0;
    while (dirent* entry = ::readdir(fdinfo)) {
        if (!all_digits(entry->d_name)) {
            continue;
        }
        const int fd = std::atoi(entry->d_name);
        if (fd <= 2) {
            continue;
        }

        const std::string path = std::string("/proc/self/fdinfo/") + entry->d_name;
        if (fdinfo_has_inotify(path) && ::close(fd) == 0) {
            ++closed;
        }
    }

    ::closedir(fdinfo);
    return closed;
}

// 统计其他进程持有的 inotify watcher
std::size_t count_external_watchers() {
    DIR* proc = ::opendir("/proc");
    if (proc == nullptr) {
        return 0;
    }

    std::size_t count = 0;
    while (dirent* proc_entry = ::readdir(proc)) {
        if (!all_digits(proc_entry->d_name)) {
            continue;
        }
        const std::string fdinfo_root = std::string("/proc/") + proc_entry->d_name + "/fdinfo";
        DIR* fdinfo = ::opendir(fdinfo_root.c_str());
        if (fdinfo == nullptr) {
            continue;
        }
        while (dirent* fd_entry = ::readdir(fdinfo)) {
            if (!all_digits(fd_entry->d_name)) {
                continue;
            }
            const std::string path = fdinfo_root + "/" + fd_entry->d_name;
            if (fdinfo_has_inotify(path)) {
                ++count;
            }
        }
        ::closedir(fdinfo);
    }

    ::closedir(proc);
    return count;
}

} // namespace

// 清理当前进程 watcher，并报告外部 watcher 是否仍存在
RuntimeResult InotifyManager::clear_watchers() {
    const std::size_t closed = close_self_inotify_fds();
    const std::size_t external = count_external_watchers();
    if (external != 0) {
        std::ostringstream detail;
        detail << "closed " << closed << " local inotify fd(s); "
               << external << " external watcher fd(s) require owner process control";
        return RuntimeResult{RuntimeStatus::Denied, detail.str()};
    }

    std::ostringstream detail;
    detail << "closed " << closed << " local inotify fd(s)";
    return RuntimeResult{RuntimeStatus::Ok, detail.str()};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
