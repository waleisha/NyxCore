#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace stack {

// dladdr 查询结果
struct DlInfoResult {
    // 符号名
    std::string symbol;
    // 模块路径
    std::string module_path;
    // 模块基址
    std::uintptr_t module_base = 0;
    // PC 相对模块的偏移
    std::uintptr_t module_offset = 0;
};

// 归一化代码地址，清理 ABI 使用的标记位
inline std::uintptr_t code_address(std::uintptr_t address) {
#if defined(__aarch64__)
    return address & 0x00ffffffffffffffull;
#elif defined(__arm__)
    return address & ~static_cast<std::uintptr_t>(1);
#else
    return address;
#endif
}

// 归一化数据地址，清理 ABI 使用的标记位
inline std::uintptr_t data_address(std::uintptr_t address) {
#if defined(__aarch64__)
    return address & 0x00ffffffffffffffull;
#else
    return address;
#endif
}

// 判断地址是否满足指针对齐
inline bool aligned_pointer(std::uintptr_t address) {
    return address != 0 && (address % alignof(std::uintptr_t)) == 0;
}

// 判断一段内存是否完全落在 maps 条目内
bool span_in_entry(const memory::MemoryMapEntry& entry, std::uintptr_t start, std::size_t size);
// 判断 maps 条目是否像栈区域
bool stack_entry(const memory::MemoryMapEntry& entry);
// 加载当前进程 maps
RuntimeResult load_memory_map(std::vector<memory::MemoryMapEntry>* out);
// 查找包含地址的 maps 条目
bool find_map(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    memory::MemoryMapEntry* out = nullptr
);
// 判断地址范围是否映射且可读
bool mapped_readable(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    std::size_t size,
    memory::MemoryMapEntry* out = nullptr
);
// 判断地址是否位于可执行映射
bool executable_address(const std::vector<memory::MemoryMapEntry>& maps, std::uintptr_t address);
// 从可读指针槽读取一个地址值
bool readable_slot(
    const std::vector<memory::MemoryMapEntry>& maps,
    std::uintptr_t address,
    std::uintptr_t* out
);
// 反混淆 C++ 符号名
std::string demangle(const char* symbol);
// 复制并转为小写
std::string lower_copy(std::string_view value);
// 查询地址所属符号和模块
DlInfoResult query_dladdr(std::uintptr_t pc);

} // namespace stack
} // namespace runtime
} // namespace nyx
