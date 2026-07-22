#pragma once

#include "src/runtime/memory/memory_map.h"

namespace nyx {
namespace runtime {
namespace memory {

// 扫描全部可匹配区域
inline constexpr int kRangeAll = 0;
// Java/Dalvik 堆区域
inline constexpr int kRangeJavaHeap = 2;
// C/C++ heap 区域
inline constexpr int kRangeCHeap = 1;
// libc/scudo 分配区域
inline constexpr int kRangeCAlloc = 4;
// 应用数据段区域
inline constexpr int kRangeCData = 8;
// BSS 匿名区域
inline constexpr int kRangeCBss = 16;
// 无路径匿名映射区域
inline constexpr int kRangeAnonymous = 32;
// Java 相关区域
inline constexpr int kRangeJava = 65536;
// 线程栈区域
inline constexpr int kRangeStack = 64;
// ashmem 区域
inline constexpr int kRangeAshmem = 524288;
// GPU/视频相关区域
inline constexpr int kRangeVideo = 1048576;
// 未归类的可写区域
inline constexpr int kRangeOther = -2080896;
// 可疑或不适合常规扫描的区域
inline constexpr int kRangeBBad = 131072;
// 应用代码区域
inline constexpr int kRangeCodeApp = 16384;
// 系统代码区域
inline constexpr int kRangeCodeSystem = 32768;

// 判断区域 ID 是否受支持
bool is_area_id(int area);
// 判断 maps 条目是否匹配指定区域
bool matches_area(const MemoryMapEntry& entry, int area);
// 判断 maps 条目是否匹配自定义文本过滤
bool matches_custom_area(const MemoryMapEntry& entry, const std::string& text);

} // namespace memory
} // namespace runtime
} // namespace nyx
