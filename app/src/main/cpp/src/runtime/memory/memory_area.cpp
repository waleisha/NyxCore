#include "src/runtime/memory/memory_area.h"

#include <initializer_list>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 判断文本是否包含指定片段
bool has(const std::string& text, const char* needle) {
    return needle != nullptr && text.find(needle) != std::string::npos;
}

// 判断文本是否包含任意片段
bool has_any(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (has(text, needle)) {
            return true;
        }
    }
    return false;
}

// 是否可读可写
bool is_rw(const MemoryMapEntry& entry) {
    return entry.readable() && entry.writable();
}

// 是否可读可执行
bool is_code(const MemoryMapEntry& entry) {
    return entry.readable() && entry.executable();
}

// 判断路径是否属于应用侧
bool is_app_path(const std::string& path) {
    return has_any(path, {"/data/app/", "/data/data/", "/data/user/"});
}

// 判断路径是否属于系统侧
bool is_system_path(const std::string& path) {
    return has_any(path, {"/system", "/vendor", "/apex", "/memfd", "[vdso"});
}

// 判断是否已经属于已知可写区域
bool matches_known_rw_area(const MemoryMapEntry& entry) {
    return matches_area(entry, kRangeJavaHeap) ||
        matches_area(entry, kRangeCHeap) ||
        matches_area(entry, kRangeCAlloc) ||
        matches_area(entry, kRangeCData) ||
        matches_area(entry, kRangeCBss) ||
        matches_area(entry, kRangeAnonymous) ||
        matches_area(entry, kRangeStack) ||
        matches_area(entry, kRangeAshmem) ||
        matches_area(entry, kRangeVideo) ||
        matches_area(entry, kRangeBBad);
}

} // namespace

// 判断区域 ID 是否受支持
bool is_area_id(int area) {
    switch (area) {
        case kRangeAll:
        case kRangeJavaHeap:
        case kRangeCHeap:
        case kRangeCAlloc:
        case kRangeCData:
        case kRangeCBss:
        case kRangeAnonymous:
        case kRangeJava:
        case kRangeStack:
        case kRangeAshmem:
        case kRangeVideo:
        case kRangeOther:
        case kRangeBBad:
        case kRangeCodeApp:
        case kRangeCodeSystem:
            return true;
        default:
            return false;
    }
}

// 判断 maps 条目是否匹配指定区域
bool matches_area(const MemoryMapEntry& entry, int area) {
    const std::string& path = entry.path;

    switch (area) {
        case kRangeAll:
            return true;
        case kRangeJavaHeap:
        case kRangeJava:
            return is_rw(entry) && has(path, "dalvik-");
        case kRangeCHeap:
            return is_rw(entry) && has(path, "[heap]");
        case kRangeCAlloc:
            return is_rw(entry) && (has(path, "[anon:libc_malloc]") || has(path, "[anon:scudo"));
        case kRangeCData:
            return entry.readable() &&
                !entry.executable() &&
                is_app_path(path) &&
                !has(path, ".ttf");
        case kRangeCBss:
            return is_rw(entry) && has(path, "[anon:.bss]");
        case kRangeAnonymous:
            return is_rw(entry) && path.empty();
        case kRangeStack:
            return is_rw(entry) && has(path, "[stack");
        case kRangeAshmem:
            return is_rw(entry) && !entry.executable() && has(path, "/dev/ashmem/") && !has(path, "dalvik");
        case kRangeVideo:
            return is_rw(entry) && has(path, "/dev/mali");
        case kRangeBBad:
            return entry.readable() && (has(path, "kgsl-3d0") || has(path, ".ttf"));
        case kRangeCodeApp:
            return is_code(entry) &&
                (is_app_path(path) || (has(path, "/dev/ashmem/") && has(path, "dalvik")));
        case kRangeCodeSystem:
            return is_code(entry) && is_system_path(path);
        case kRangeOther:
            return is_rw(entry) &&
                !matches_known_rw_area(entry) &&
                !is_system_path(path) &&
                !is_app_path(path);
        default:
            return false;
    }
}

// 判断 maps 条目是否匹配自定义文本过滤
bool matches_custom_area(const MemoryMapEntry& entry, const std::string& text) {
    if (text.empty()) {
        return false;
    }

    return entry.path.find(text) != std::string::npos ||
        entry.permissions.find(text) != std::string::npos ||
        entry.device.find(text) != std::string::npos ||
        entry.name().find(text) != std::string::npos;
}

} // namespace memory
} // namespace runtime
} // namespace nyx
