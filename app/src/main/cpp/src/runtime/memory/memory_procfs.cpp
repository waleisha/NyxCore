#include "src/runtime/memory/memory_map.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

#include <algorithm>
#include <cctype>
#include <string>

namespace nyx {
namespace runtime {
namespace memory {
namespace detail {

namespace {

// 去掉 maps 路径字段前的空白
std::string trim_left(std::string text) {
    const auto first = std::find_if(text.begin(), text.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    text.erase(text.begin(), first);
    return text;
}

// 解析十六进制地址字段
bool parse_hex(const std::string& text, std::uintptr_t* out) {
    if (out == nullptr || text.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 16);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }

    if (value > static_cast<unsigned long long>(std::numeric_limits<std::uintptr_t>::max())) {
        return false;
    }

    *out = static_cast<std::uintptr_t>(value);
    return true;
}

// 解析十进制 inode 字段
bool parse_u64(const std::string& text, std::uint64_t* out) {
    if (out == nullptr || text.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }

    *out = static_cast<std::uint64_t>(value);
    return true;
}

} // namespace

// 读取并解析 procfs maps 文件
RuntimeResult read_maps(const std::string& path, std::vector<MemoryMapEntry>* out) {
    if (path.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing maps path or output map"};
    }
    out->clear();

    std::ifstream input(path);
    if (!input.is_open()) {
        return RuntimeResult{RuntimeStatus::Unavailable, "failed to open " + path};
    }

    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);

        std::string range;
        std::string offset;
        std::string inode;
        MemoryMapEntry entry;
        if (!(stream >> range >> entry.permissions >> offset >> entry.device >> inode)) {
            continue;
        }

        const std::size_t dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }

        const std::string start_text = range.substr(0, dash);
        const std::string end_text = range.substr(dash + 1);
        if (!parse_hex(start_text, &entry.start) ||
            !parse_hex(end_text, &entry.end) ||
            !parse_hex(offset, &entry.offset) ||
            !parse_u64(inode, &entry.inode) ||
            entry.start >= entry.end) {
            continue;
        }

        std::string path;
        std::getline(stream, path);
        entry.path = trim_left(path);
        out->push_back(entry);
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::Unavailable, path + " produced no entries"};
    }

    return RuntimeResult{};
}

} // namespace detail
} // namespace memory
} // namespace runtime
} // namespace nyx
