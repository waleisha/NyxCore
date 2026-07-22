#include "src/runtime/stack/stack_base.h"

#include <cxxabi.h>
#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace nyx {
namespace runtime {
namespace stack {

// 判断一段内存是否完全落在 maps 条目内
bool span_in_entry(const memory::MemoryMapEntry& entry, std::uintptr_t start, std::size_t size) {
    start = data_address(start);
    if (start == 0 || size == 0) {
        return false;
    }

    const std::uintptr_t end = start + static_cast<std::uintptr_t>(size - 1);
    return end >= start && entry.contains(start) && entry.contains(end);
}

// 判断 maps 条目是否像栈区域
bool stack_entry(const memory::MemoryMapEntry& entry) {
    return entry.readable() && entry.writable() && !entry.executable();
}

// 加载当前进程 maps
RuntimeResult load_memory_map(std::vector<memory::MemoryMapEntry>* out) {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory map output"};
    }

    memory::MemoryMap memory_map;
    return memory_map.current(out);
}

// 查找包含地址的 maps 条目
bool find_map(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    memory::MemoryMapEntry* out
) {
    if (address == 0) {
        return false;
    }

    const auto found = std::find_if(maps.begin(), maps.end(), [address](const memory::MemoryMapEntry& entry) {
        return entry.contains(address);
    });
    if (found == maps.end()) {
        return false;
    }

    if (out != nullptr) {
        *out = *found;
    }
    return true;
}

// 判断地址范围是否映射且可读
bool mapped_readable(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    std::size_t size,
    memory::MemoryMapEntry* out
) {
    address = data_address(address);
    memory::MemoryMapEntry entry;
    if (!find_map(maps, address, &entry) || !span_in_entry(entry, address, size) || !entry.readable()) {
        return false;
    }

    if (out != nullptr) {
        *out = entry;
    }
    return true;
}

// 判断地址是否位于可执行映射
bool executable_address(const std::vector<memory::MemoryMapEntry>& maps, std::uintptr_t address) {
    memory::MemoryMapEntry entry;
    return find_map(maps, code_address(address), &entry) && entry.executable();
}

// 从可读指针槽读取一个地址值
bool readable_slot(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    std::uintptr_t* out
) {
    address = data_address(address);
    if (out == nullptr || !aligned_pointer(address) ||
        !mapped_readable(maps, address, sizeof(std::uintptr_t))) {
        return false;
    }

    *out = *reinterpret_cast<const std::uintptr_t*>(address);
    return true;
}

// 反混淆 C++ 符号名
std::string demangle(const char* symbol) {
    if (symbol == nullptr || symbol[0] == '\0') {
        return {};
    }

    int status = 0;
    char* demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
    if (status != 0 || demangled == nullptr) {
        return symbol;
    }

    std::string result(demangled);
    std::free(demangled);
    return result;
}

// 复制并转为小写
std::string lower_copy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

// 查询地址所属符号和模块
DlInfoResult query_dladdr(std::uintptr_t pc) {
    DlInfoResult out;
    pc = code_address(pc);
    if (pc == 0) {
        return out;
    }

    Dl_info info{};
    if (::dladdr(reinterpret_cast<void*>(pc), &info) == 0) {
        return out;
    }
    if (info.dli_sname != nullptr) {
        out.symbol = demangle(info.dli_sname);
    }
    if (info.dli_fname != nullptr) {
        out.module_path = info.dli_fname;
    }
    if (info.dli_fbase != nullptr) {
        out.module_base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        if (pc >= out.module_base) {
            out.module_offset = pc - out.module_base;
        }
    }

    return out;
}

} // namespace stack
} // namespace runtime
} // namespace nyx
