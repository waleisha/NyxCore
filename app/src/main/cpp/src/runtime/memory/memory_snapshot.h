#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/memory/memory_map.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// VMA 快照
struct VmaSnapshot {
    // 快照 ID
    std::uint64_t id = 0;
    // 采集时的单调时钟纳秒
    std::uint64_t monotonic_time_ns = 0;
    // maps 条目
    std::vector<MemoryMapEntry> entries;
};

// VMA 差异类型
enum class VmaDiffKind {
    // 新增映射
    Added,
    // 移除映射
    Removed,
    // 地址范围变化
    RangeChanged,
    // 权限变化
    PermissionChanged,
    // 名称或路径变化
    NameChanged,
    // 未变化
    Unchanged,
};

// 单条 VMA 差异
struct VmaDiffEntry {
    // 差异类型
    VmaDiffKind kind = VmaDiffKind::Unchanged;
    // 变化前条目
    MemoryMapEntry before;
    // 变化后条目
    MemoryMapEntry after;
    // 差异详情
    std::string detail;
};

// VMA 快照差异
struct VmaDiff {
    // 变化前快照
    VmaSnapshot before;
    // 变化后快照
    VmaSnapshot after;
    // 差异条目
    std::vector<VmaDiffEntry> entries;
};

// VMA 快照工具
class VmaSnapshotter {
public:
    // 采集当前 VMA 快照
    RuntimeResult capture(VmaSnapshot* out) const;
    // 比较两份 VMA 快照
    RuntimeResult diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out) const;
};

} // namespace memory
} // namespace runtime
} // namespace nyx
