#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/memory/memory_process.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// /proc/<pid>/maps 中的一条 VMA 记录
struct MemoryMapEntry {
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址，左闭右开
    std::uintptr_t end = 0;
    // 权限字符串，如 r-xp
    std::string permissions;
    // 文件偏移
    std::uintptr_t offset = 0;
    // 设备号文本
    std::string device;
    // inode
    std::uint64_t inode = 0;
    // 映射路径或匿名名称
    std::string path;

    // 映射大小
    std::size_t size() const;
    // 判断地址是否落在该映射内
    bool contains(std::uintptr_t address) const;
    // 是否可读
    bool readable() const;
    // 是否可写
    bool writable() const;
    // 是否可执行
    bool executable() const;
    // 是否共享映射
    bool shared() const;
    // 是否私有映射
    bool private_map() const;
    // 是否看起来是共享库
    bool is_library() const;
    // 获取路径 basename
    std::string name() const;
};

// 合并后的共享库信息
struct MemoryLibrary {
    // 库文件名
    std::string name;
    // 库路径
    std::string path;
    // 库最小起始地址
    std::uintptr_t start = 0;
    // 库最大结束地址
    std::uintptr_t end = 0;
    // 库的 maps 段
    std::vector<MemoryMapEntry> segments;

    // 库覆盖范围大小
    std::size_t size() const;
    // 是否有可读段
    bool readable() const;
    // 是否有可写段
    bool writable() const;
    // 是否有可执行段
    bool executable() const;
};

// 进程内存映射读取器
class MemoryMap {
public:
    // 默认读取当前进程 maps
    MemoryMap();
    // 读取指定进程 maps
    explicit MemoryMap(MemProcess process);

    // 获取当前 maps 快照
    RuntimeResult current(std::vector<MemoryMapEntry>* out) const;
    // 获取共享库列表
    RuntimeResult libraries(std::vector<MemoryLibrary>* out) const;
    // 查找包含地址的映射
    RuntimeResult find_address(std::uintptr_t address, MemoryMapEntry* out) const;
    // 查找匹配名称的库段
    RuntimeResult find_library(const std::string& name, std::vector<MemoryMapEntry>* out) const;

private:
    // 目标进程
    MemProcess process_;
    // 懒加载缓存
    mutable std::vector<MemoryMapEntry> cached_entries_;
    // 缓存是否已加载
    mutable bool cached_ = false;

    // 确保缓存已加载
    RuntimeResult ensure_loaded() const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
