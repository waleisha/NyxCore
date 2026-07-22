#include "src/runtime/memory/memory_map.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace nyx {
namespace runtime {
namespace memory {

namespace detail {

RuntimeResult read_maps(const std::string& path, std::vector<MemoryMapEntry>* out);
void append_linker_libraries(const std::vector<MemoryMapEntry>& entries, std::vector<MemoryLibrary>* libraries);
void append_linker_entries(
    const std::vector<MemoryMapEntry>& entries,
    const std::string& name,
    std::vector<MemoryMapEntry>* out
);

} // namespace detail

namespace {

// 获取路径 basename
std::string base_name(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

// 判断 maps 条目是否匹配库名或路径
bool matches_name(const MemoryMapEntry& entry, const std::string& name) {
    if (name.empty()) {
        return false;
    }

    if (entry.path == name || entry.name() == name) {
        return true;
    }

    return entry.path.find(name) != std::string::npos;
}

} // namespace

// 映射大小
std::size_t MemoryMapEntry::size() const {
    if (end <= start) {
        return 0;
    }

    const auto span = end - start;
    if (span > static_cast<std::uintptr_t>(std::numeric_limits<std::size_t>::max())) {
        return std::numeric_limits<std::size_t>::max();
    }

    return static_cast<std::size_t>(span);
}

// 判断地址是否落在该映射内
bool MemoryMapEntry::contains(std::uintptr_t address) const {
    return start <= address && address < end;
}

// 是否可读
bool MemoryMapEntry::readable() const {
    return !permissions.empty() && permissions[0] == 'r';
}

// 是否可写
bool MemoryMapEntry::writable() const {
    return permissions.size() > 1 && permissions[1] == 'w';
}

// 是否可执行
bool MemoryMapEntry::executable() const {
    return permissions.size() > 2 && permissions[2] == 'x';
}

// 是否共享映射
bool MemoryMapEntry::shared() const {
    return permissions.size() > 3 && permissions[3] == 's';
}

// 是否私有映射
bool MemoryMapEntry::private_map() const {
    return permissions.size() > 3 && permissions[3] == 'p';
}

// 是否看起来是共享库
bool MemoryMapEntry::is_library() const {
    return path.find(".so") != std::string::npos;
}

// 获取路径 basename
std::string MemoryMapEntry::name() const {
    return base_name(path);
}

// 默认读取当前进程 maps
MemoryMap::MemoryMap()
    : process_(MemProcess::current()) {}

// 读取指定进程 maps
MemoryMap::MemoryMap(MemProcess process)
    : process_(std::move(process)) {}

// 库覆盖范围大小
std::size_t MemoryLibrary::size() const {
    if (end <= start) {
        return 0;
    }

    const auto span = end - start;
    if (span > static_cast<std::uintptr_t>(std::numeric_limits<std::size_t>::max())) {
        return std::numeric_limits<std::size_t>::max();
    }

    return static_cast<std::size_t>(span);
}

// 是否有可读段
bool MemoryLibrary::readable() const {
    return std::any_of(segments.begin(), segments.end(), [](const MemoryMapEntry& entry) {
        return entry.readable();
    });
}

// 是否有可写段
bool MemoryLibrary::writable() const {
    return std::any_of(segments.begin(), segments.end(), [](const MemoryMapEntry& entry) {
        return entry.writable();
    });
}

// 是否有可执行段
bool MemoryLibrary::executable() const {
    return std::any_of(segments.begin(), segments.end(), [](const MemoryMapEntry& entry) {
        return entry.executable();
    });
}

// 确保缓存已加载
RuntimeResult MemoryMap::ensure_loaded() const {
    if (cached_) {
        return RuntimeResult{};
    }
    auto result = detail::read_maps(process_.maps_path, &cached_entries_);
    if (result.ok()) {
        cached_ = true;
    }
    return result;
}

// 获取当前 maps 快照
RuntimeResult MemoryMap::current(std::vector<MemoryMapEntry>* out) const {
    return detail::read_maps(process_.maps_path, out);
}

// 获取共享库列表
RuntimeResult MemoryMap::libraries(std::vector<MemoryLibrary>* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing output libraries"};
    }
    out->clear();

    std::vector<MemoryMapEntry> entries;
    const auto result = detail::read_maps(process_.maps_path, &entries);
    if (!result.ok()) {
        return result;
    }


    std::unordered_map<std::string, std::size_t> by_path;
    for (const auto& entry : entries) {
        if (!entry.is_library()) {
            continue;
        }

        const std::string key = entry.path.empty() ? entry.name() : entry.path;
        const auto found = by_path.find(key);
        if (found == by_path.end()) {
            MemoryLibrary library;
            library.name = entry.name();
            library.path = entry.path;
            library.start = entry.start;
            library.end = entry.end;
            library.segments.push_back(entry);
            by_path.emplace(key, out->size());
            out->push_back(std::move(library));
            continue;
        }

        MemoryLibrary& library = (*out)[found->second];
        library.start = std::min(library.start, entry.start);
        library.end = std::max(library.end, entry.end);
        library.segments.push_back(entry);
    }

    detail::append_linker_libraries(entries, out);

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "no shared libraries found in memory map"};
    }

    return RuntimeResult{};
}

// 查找包含地址的映射
RuntimeResult MemoryMap::find_address(std::uintptr_t address, MemoryMapEntry* out) const {
    if (address == 0 || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing address or output map entry"};
    }
    *out = MemoryMapEntry{};

    auto loaded = ensure_loaded();
    if (!loaded.ok()) {
        return loaded;
    }

    const auto found = std::find_if(cached_entries_.begin(), cached_entries_.end(), [address](const MemoryMapEntry& entry) {
        return entry.contains(address);
    });
    if (found == cached_entries_.end()) {
        return RuntimeResult{RuntimeStatus::NotFound, "address is not mapped"};
    }

    *out = *found;
    return RuntimeResult{};
}

// 查找匹配名称的库段
RuntimeResult MemoryMap::find_library(const std::string& name, std::vector<MemoryMapEntry>* out) const {
    if (name.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing library name or output entries"};
    }
    out->clear();

    auto loaded = ensure_loaded();
    if (!loaded.ok()) {
        return loaded;
    }

    for (const auto& entry : cached_entries_) {
        if (entry.is_library() && matches_name(entry, name)) {
            out->push_back(entry);
        }
    }

    if (out->empty()) {
        detail::append_linker_entries(cached_entries_, name, out);
    }

    if (out->empty()) {
        return RuntimeResult{RuntimeStatus::NotFound, "library was not found in memory map"};
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
