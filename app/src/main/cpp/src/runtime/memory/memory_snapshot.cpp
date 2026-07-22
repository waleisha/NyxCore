#include "src/runtime/memory/memory_snapshot.h"

#include <time.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 快照 ID 计数器
std::atomic<std::uint64_t> g_snapshot_id{0};

// 读取单调时钟纳秒时间
std::uint64_t monotonic_time_ns() {
    timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
        static_cast<std::uint64_t>(ts.tv_nsec);
}

// 判断路径是否为匿名映射
bool is_anon_path(const std::string& path) {
    return path.empty() || path.rfind("[anon:", 0) == 0;
}

// 判断两个 VMA 是否拥有完全一致的身份
bool same_key(const MemoryMapEntry& left, const MemoryMapEntry& right) {
    return left.start == right.start &&
        left.end == right.end &&
        left.offset == right.offset &&
        left.device == right.device &&
        left.inode == right.inode &&
        left.path == right.path;
}

// 判断两个 VMA 是否保持同一地址范围身份
bool same_range_identity(const MemoryMapEntry& left, const MemoryMapEntry& right) {
    return left.start == right.start &&
        left.end == right.end &&
        left.offset == right.offset &&
        left.device == right.device &&
        left.inode == right.inode;
}

// 判断两个 VMA 是否来自同一文件偏移
bool same_file_identity(const MemoryMapEntry& left, const MemoryMapEntry& right) {
    return !left.path.empty() &&
        left.path == right.path &&
        left.inode != 0 &&
        left.inode == right.inode &&
        left.offset == right.offset;
}

// 判断两个 VMA 范围是否重叠
bool overlaps(const MemoryMapEntry& left, const MemoryMapEntry& right) {
    return left.start < right.end && right.start < left.end;
}

// 为快照差异匹配计算可信度
int match_score(const MemoryMapEntry& before, const MemoryMapEntry& after) {
    if (same_key(before, after)) {
        return 100;
    }
    if (same_range_identity(before, after)) {
        return 80;
    }
    if (same_file_identity(before, after)) {
        return 60;
    }
    if (is_anon_path(before.path) && is_anon_path(after.path) && overlaps(before, after)) {
        return 40;
    }

    return 0;
}

// 生成 VMA 差异的可读描述
std::string diff_detail(VmaDiffKind kind, const MemoryMapEntry& before, const MemoryMapEntry& after) {
    switch (kind) {
        case VmaDiffKind::Added:
            return "vma added";
        case VmaDiffKind::Removed:
            return "vma removed";
        case VmaDiffKind::RangeChanged:
            return "vma range changed";
        case VmaDiffKind::PermissionChanged:
            return before.permissions + " -> " + after.permissions;
        case VmaDiffKind::NameChanged:
            return before.path + " -> " + after.path;
        case VmaDiffKind::Unchanged:
            return "vma unchanged";
    }

    return "vma changed";
}

// 按最明显的变化类型分类
VmaDiffKind classify_change(const MemoryMapEntry& before, const MemoryMapEntry& after) {
    if (before.start != after.start || before.end != after.end) {
        return VmaDiffKind::RangeChanged;
    }
    if (before.permissions != after.permissions) {
        return VmaDiffKind::PermissionChanged;
    }
    if (before.path != after.path) {
        return VmaDiffKind::NameChanged;
    }

    return VmaDiffKind::Unchanged;
}

} // namespace

// 捕获当前进程 VMA 快照
RuntimeResult VmaSnapshotter::capture(VmaSnapshot* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma snapshot output"};
    }
    *out = VmaSnapshot{};

    MemoryMap memory_map;
    std::vector<MemoryMapEntry> entries;
    auto result = memory_map.current(&entries);
    if (!result.ok()) {
        return result;
    }

    std::sort(entries.begin(), entries.end(), [](const MemoryMapEntry& left, const MemoryMapEntry& right) {
        if (left.start != right.start) {
            return left.start < right.start;
        }
        return left.end < right.end;
    });

    out->id = g_snapshot_id.fetch_add(1, std::memory_order_relaxed) + 1;
    out->monotonic_time_ns = monotonic_time_ns();
    out->entries = std::move(entries);
    return RuntimeResult{};
}

// 对比两个 VMA 快照
RuntimeResult VmaSnapshotter::diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out) const {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing vma diff output"};
    }
    *out = VmaDiff{};
    out->before = before;
    out->after = after;

    // 先按稳定主键索引旧快照，精确匹配可以 O(1) 查找。
    struct KeyHash {
        std::size_t operator()(const std::tuple<std::uintptr_t, std::uintptr_t, std::uintptr_t, std::string, std::uint64_t>& k) const {
            auto [start, end, offset, device, inode] = k;
            auto h1 = std::hash<std::uintptr_t>{}(start);
            auto h2 = std::hash<std::uintptr_t>{}(end);
            auto h3 = std::hash<std::string>{}(device);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (inode * 2654435761ull);
        }
    };
    using FullKey = std::tuple<std::uintptr_t, std::uintptr_t, std::uintptr_t, std::string, std::uint64_t>;
    std::unordered_map<FullKey, std::size_t, KeyHash> before_by_key;
    for (std::size_t i = 0; i < before.entries.size(); ++i) {
        const auto& e = before.entries[i];
        before_by_key.emplace(FullKey{e.start, e.end, e.offset, e.device, e.inode}, i);
    }

    std::vector<bool> used(before.entries.size(), false);

    // 第一轮处理完全一致的 VMA，避免后续模糊匹配误占。
    for (const auto& after_entry : after.entries) {
        FullKey key{after_entry.start, after_entry.end, after_entry.offset, after_entry.device, after_entry.inode};
        auto found = before_by_key.find(key);
        if (found != before_by_key.end() && !used[found->second] &&
            before.entries[found->second].path == after_entry.path) {
            used[found->second] = true;
            const auto& before_entry = before.entries[found->second];
            const VmaDiffKind kind = classify_change(before_entry, after_entry);
            if (kind != VmaDiffKind::Unchanged) {
                VmaDiffEntry diff_entry;
                diff_entry.kind = kind;
                diff_entry.before = before_entry;
                diff_entry.after = after_entry;
                diff_entry.detail = diff_detail(kind, before_entry, after_entry);
                out->entries.push_back(std::move(diff_entry));
            }
            continue;
        }
    }

    // 第二轮只处理未匹配项，范围身份、文件身份和匿名重叠都在这里兜底。
    for (const auto& after_entry : after.entries) {
        FullKey key{after_entry.start, after_entry.end, after_entry.offset, after_entry.device, after_entry.inode};
        auto exact = before_by_key.find(key);
        if (exact != before_by_key.end() && used[exact->second]) {
            continue; // 第一轮已经匹配
        }

        int best_score = 0;
        std::size_t best_index = before.entries.size();
        for (std::size_t i = 0; i < before.entries.size(); ++i) {
            if (used[i]) {
                continue;
            }
            const int score = match_score(before.entries[i], after_entry);
            if (score > best_score) {
                best_score = score;
                best_index = i;
            }
        }

        if (best_index == before.entries.size()) {
            VmaDiffEntry diff_entry;
            diff_entry.kind = VmaDiffKind::Added;
            diff_entry.after = after_entry;
            diff_entry.detail = diff_detail(diff_entry.kind, diff_entry.before, diff_entry.after);
            out->entries.push_back(std::move(diff_entry));
            continue;
        }

        used[best_index] = true;
        const auto& before_entry = before.entries[best_index];
        const VmaDiffKind kind = classify_change(before_entry, after_entry);
        if (kind == VmaDiffKind::Unchanged) {
            continue;
        }

        VmaDiffEntry diff_entry;
        diff_entry.kind = kind;
        diff_entry.before = before_entry;
        diff_entry.after = after_entry;
        diff_entry.detail = diff_detail(kind, before_entry, after_entry);
        out->entries.push_back(std::move(diff_entry));
    }

    // 剩余未使用的旧条目都视为已移除
    for (std::size_t i = 0; i < before.entries.size(); ++i) {
        if (used[i]) {
            continue;
        }

        VmaDiffEntry diff_entry;
        diff_entry.kind = VmaDiffKind::Removed;
        diff_entry.before = before.entries[i];
        diff_entry.detail = diff_detail(diff_entry.kind, diff_entry.before, diff_entry.after);
        out->entries.push_back(std::move(diff_entry));
    }

    return RuntimeResult{};
}

} // namespace memory
} // namespace runtime
} // namespace nyx
