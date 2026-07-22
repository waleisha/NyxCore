#include "src/runtime/memory/memory_map.h"

#include <link.h>

#include <algorithm>
#include <string>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {
namespace detail {

namespace {

// 链接器段范围
struct Range {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
};

// dl_iterate_phdr 枚举到的模块
struct LinkModule {
    std::string name;
    std::string path;
    std::vector<Range> ranges;
};

// 提取路径中的文件名
std::string base_name(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

// 判断 maps 条目和链接器段是否相交
bool intersects(const MemoryMapEntry& entry, const Range& range) {
    return entry.start < range.end && range.start < entry.end;
}

// 判断模块是否命中查询名
bool matches_module(const LinkModule& module, const std::string& name) {
    if (name.empty()) {
        return false;
    }

    return module.name == name || module.path == name || module.path.find(name) != std::string::npos;
}

// 判断库记录是否已经覆盖链接器模块
bool overlaps_module(const MemoryLibrary& library, const LinkModule& module) {
    for (const auto& entry : library.segments) {
        for (const auto& range : module.ranges) {
            if (intersects(entry, range)) {
                return true;
            }
        }
    }
    return false;
}

// 从 maps 条目中筛出属于链接器模块的段
std::vector<MemoryMapEntry> entries_for(const std::vector<MemoryMapEntry>& entries, const LinkModule& module) {
    std::vector<MemoryMapEntry> out;
    for (const auto& entry : entries) {
        for (const auto& range : module.ranges) {
            if (intersects(entry, range)) {
                out.push_back(entry);
                break;
            }
        }
    }
    return out;
}

// 从模块段组装库记录
bool library_from(
    const std::string& name,
    const std::string& path,
    const std::vector<MemoryMapEntry>& segments,
    MemoryLibrary* out
) {
    if (out == nullptr || segments.empty()) {
        return false;
    }

    MemoryLibrary library;
    library.name = name.empty() ? segments.front().name() : name;
    library.path = path.empty() ? segments.front().path : path;
    library.start = segments.front().start;
    library.end = segments.front().end;
    library.segments = segments;

    for (const auto& segment : segments) {
        library.start = std::min(library.start, segment.start);
        library.end = std::max(library.end, segment.end);
    }

    *out = library;
    return true;
}

// 判断库列表中是否已存在该链接器模块
bool has_module(const std::vector<MemoryLibrary>& libraries, const LinkModule& module) {
    for (const auto& library : libraries) {
        if (overlaps_module(library, module)) {
            return true;
        }
    }
    return false;
}

// 收集当前进程已加载的 so 模块
int collect_module(dl_phdr_info* info, std::size_t, void* data) {
    if (info == nullptr || data == nullptr || info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
        return 0;
    }

    std::string path = info->dlpi_name;
    if (path.find(".so") == std::string::npos) {
        return 0;
    }

    LinkModule module;
    module.name = base_name(path);
    module.path = path;

    for (std::uint16_t i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& header = info->dlpi_phdr[i];
        if (header.p_type != PT_LOAD || header.p_memsz == 0) {
            continue;
        }

        const auto start = static_cast<std::uintptr_t>(info->dlpi_addr + header.p_vaddr);
        module.ranges.push_back(Range{start, start + static_cast<std::uintptr_t>(header.p_memsz)});
    }

    if (!module.ranges.empty()) {
        auto* modules = static_cast<std::vector<LinkModule>*>(data);
        modules->push_back(module);
    }

    return 0;
}

// 枚举链接器视角的模块列表
std::vector<LinkModule> link_modules() {
    std::vector<LinkModule> modules;
    dl_iterate_phdr(collect_module, &modules);
    return modules;
}

} // namespace

// 将链接器中可见但 maps 聚合遗漏的库补充进去
void append_linker_libraries(const std::vector<MemoryMapEntry>& entries, std::vector<MemoryLibrary>* libraries) {
    if (libraries == nullptr) {
        return;
    }

    for (const auto& module : link_modules()) {
        if (has_module(*libraries, module)) {
            continue;
        }

        MemoryLibrary library;
        if (!library_from(module.name, module.path, entries_for(entries, module), &library)) {
            continue;
        }

        libraries->push_back(std::move(library));
    }
}

// 将指定模块的链接器映射条目补充到结果集
void append_linker_entries(
    const std::vector<MemoryMapEntry>& entries,
    const std::string& name,
    std::vector<MemoryMapEntry>* out
) {
    if (out == nullptr) {
        return;
    }

    for (const auto& module : link_modules()) {
        if (!matches_module(module, name)) {
            continue;
        }

        auto module_entries = entries_for(entries, module);
        out->insert(out->end(), module_entries.begin(), module_entries.end());
    }
}

} // namespace detail
} // namespace memory
} // namespace runtime
} // namespace nyx
