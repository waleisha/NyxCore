#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// 32 位整数类型
inline constexpr int kTypeDword = 4;
// 32 位浮点类型
inline constexpr int kTypeFloat = 16;
// 64 位浮点类型
inline constexpr int kTypeDouble = 64;
// 16 位整数类型
inline constexpr int kTypeWord = 2;
// 8 位整数类型
inline constexpr int kTypeByte = 1;
// 64 位整数类型
inline constexpr int kTypeQword = 32;

// 已解析的标量值
struct ScalarValue {
    // 值类型
    int type = kTypeDword;
    // 小端字节表示
    std::array<std::uint8_t, 8> bytes = {};
    // 有效字节数
    std::size_t size = 0;
    // 数值表示，用于范围比较
    long double number = 0;
};

// 标量范围
struct ValueRange {
    // 最小值
    ScalarValue min;
    // 最大值
    ScalarValue max;
};

// 搜索条件
struct SearchTerm {
    // 搜索值类型
    int type = kTypeDword;
    // 是否为范围搜索
    bool range = false;
    // 精确匹配值
    ScalarValue value;
    // 范围匹配边界
    ValueRange bounds;
};

// 联合搜索条件
struct UnitedSearch {
    // 多个搜索项
    std::vector<SearchTerm> terms;
    // 后续项允许出现的跨度
    std::size_t span = 512;
};

// 判断类型 ID 是否受支持
bool is_type_id(int type);
// 获取类型字节数
std::size_t type_size(int type);
// 解析标量值文本
RuntimeResult parse_value(const char* text, int type, ScalarValue* out);
// 解析精确或范围搜索文本
RuntimeResult parse_search(const char* text, int type, SearchTerm* out);
// 解析范围搜索文本
RuntimeResult parse_range(const char* text, int type, SearchTerm* out);
// 解析联合搜索文本
RuntimeResult parse_united(const char* text, int default_type, UnitedSearch* out);
// 判断内存数据是否匹配搜索条件
bool matches_value(const void* data, const SearchTerm& term);
// 将内存数据格式化为文本
std::string format_value(const void* data, int type);

} // namespace memory
} // namespace runtime
} // namespace nyx
