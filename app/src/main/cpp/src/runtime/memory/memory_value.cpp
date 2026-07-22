#include "src/runtime/memory/memory_value.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace nyx {
namespace runtime {
namespace memory {

namespace {

// 去掉首尾空白
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// 将标量写入通用字节容器
template <class T>
void write_scalar(T value, ScalarValue* out) {
    out->size = sizeof(T);
    std::memcpy(out->bytes.data(), &value, sizeof(T));
}

// 从内存中读取标量
template <class T>
T read_scalar(const void* data) {
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

// 解析整数，支持 0x 前缀
bool parse_i64(const std::string& text, long long* out) {
    if (text.empty() || out == nullptr) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 0);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }

    *out = value;
    return true;
}

// 解析浮点数
bool parse_f64(const std::string& text, long double* out) {
    if (text.empty() || out == nullptr) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long double value = std::strtold(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(static_cast<double>(value))) {
        return false;
    }

    *out = value;
    return true;
}

// 联合搜索后缀类型
int suffix_type(char suffix) {
    switch (suffix) {
        case 'D':
        case 'd':
            return kTypeDword;
        case 'F':
        case 'f':
            return kTypeFloat;
        case 'E':
        case 'e':
            return kTypeDouble;
        case 'W':
        case 'w':
            return kTypeWord;
        case 'B':
        case 'b':
            return kTypeByte;
        case 'Q':
        case 'q':
            return kTypeQword;
        default:
            return 0;
    }
}

// 从字节数据转为统一数值
long double number_from(const void* data, int type) {
    switch (type) {
        case kTypeDword:
            return static_cast<long double>(read_scalar<std::int32_t>(data));
        case kTypeFloat:
            return static_cast<long double>(read_scalar<float>(data));
        case kTypeDouble:
            return static_cast<long double>(read_scalar<double>(data));
        case kTypeWord:
            return static_cast<long double>(read_scalar<std::int16_t>(data));
        case kTypeByte:
            return static_cast<long double>(read_scalar<std::int8_t>(data));
        case kTypeQword:
            return static_cast<long double>(read_scalar<std::int64_t>(data));
        default:
            return 0;
    }
}

} // namespace

// 判断类型 ID 是否受支持
bool is_type_id(int type) {
    switch (type) {
        case kTypeDword:
        case kTypeFloat:
        case kTypeDouble:
        case kTypeWord:
        case kTypeByte:
        case kTypeQword:
            return true;
        default:
            return false;
    }
}

// 获取类型字节数
std::size_t type_size(int type) {
    switch (type) {
        case kTypeDword:
        case kTypeFloat:
            return 4;
        case kTypeDouble:
        case kTypeQword:
            return 8;
        case kTypeWord:
            return 2;
        case kTypeByte:
            return 1;
        default:
            return 0;
    }
}

// 解析标量值文本
RuntimeResult parse_value(const char* text, int type, ScalarValue* out) {
    if (text == nullptr || text[0] == '\0' || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory value"};
    }
    if (!is_type_id(type)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory value type"};
    }

    const std::string value_text = trim(text);
    if (value_text.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "empty memory value"};
    }

    ScalarValue value;
    value.type = type;
    value.size = type_size(type);

    if (type == kTypeFloat || type == kTypeDouble) {
        long double parsed = 0;
        if (!parse_f64(value_text, &parsed)) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid floating memory value"};
        }

        value.number = parsed;
        if (type == kTypeFloat) {
            write_scalar(static_cast<float>(parsed), &value);
        } else {
            write_scalar(static_cast<double>(parsed), &value);
        }
    } else {
        long long parsed = 0;
        if (!parse_i64(value_text, &parsed)) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid integer memory value"};
        }

        value.number = static_cast<long double>(parsed);
        switch (type) {
            case kTypeDword:
                if (parsed < std::numeric_limits<std::int32_t>::min() ||
                    parsed > std::numeric_limits<std::int32_t>::max()) {
                    return RuntimeResult{RuntimeStatus::InvalidArgument, "dword memory value is out of range"};
                }
                write_scalar(static_cast<std::int32_t>(parsed), &value);
                break;
            case kTypeWord:
                if (parsed < std::numeric_limits<std::int16_t>::min() ||
                    parsed > std::numeric_limits<std::int16_t>::max()) {
                    return RuntimeResult{RuntimeStatus::InvalidArgument, "word memory value is out of range"};
                }
                write_scalar(static_cast<std::int16_t>(parsed), &value);
                break;
            case kTypeByte:
                if (parsed < std::numeric_limits<std::int8_t>::min() ||
                    parsed > std::numeric_limits<std::int8_t>::max()) {
                    return RuntimeResult{RuntimeStatus::InvalidArgument, "byte memory value is out of range"};
                }
                write_scalar(static_cast<std::int8_t>(parsed), &value);
                break;
            case kTypeQword:
                write_scalar(static_cast<std::int64_t>(parsed), &value);
                break;
            default:
                return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid integer memory value type"};
        }
    }

    *out = value;
    return RuntimeResult{};
}

// 解析精确或范围搜索文本
RuntimeResult parse_search(const char* text, int type, SearchTerm* out) {
    if (out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing search term output"};
    }
    if (text != nullptr && std::string(text).find('~') != std::string::npos) {
        return parse_range(text, type, out);
    }

    ScalarValue value;
    auto parsed = parse_value(text, type, &value);
    if (!parsed.ok()) {
        return parsed;
    }

    SearchTerm term;
    term.type = type;
    term.value = value;
    *out = term;
    return RuntimeResult{};
}

// 解析范围搜索文本
RuntimeResult parse_range(const char* text, int type, SearchTerm* out) {
    if (text == nullptr || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing memory value range"};
    }

    const std::string range_text = trim(text);
    const std::size_t split = range_text.find('~');
    if (split == std::string::npos || split == 0 || split + 1 >= range_text.size()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid memory value range"};
    }

    ScalarValue min;
    ScalarValue max;
    auto parsed_min = parse_value(range_text.substr(0, split).c_str(), type, &min);
    if (!parsed_min.ok()) {
        return parsed_min;
    }
    auto parsed_max = parse_value(range_text.substr(split + 1).c_str(), type, &max);
    if (!parsed_max.ok()) {
        return parsed_max;
    }
    if (min.number > max.number) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "memory value range is inverted"};
    }

    SearchTerm term;
    term.type = type;
    term.range = true;
    term.bounds = ValueRange{min, max};
    *out = term;
    return RuntimeResult{};
}

// 解析联合搜索文本
RuntimeResult parse_united(const char* text, int default_type, UnitedSearch* out) {
    if (text == nullptr || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing united memory value"};
    }
    if (!is_type_id(default_type)) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid united default type"};
    }

    std::string source = trim(text);
    if (source.empty()) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "empty united memory value"};
    }

    std::size_t span = 512;
    const std::size_t span_split = source.find_last_of(':');
    if (span_split != std::string::npos) {
        const std::string span_text = trim(source.substr(span_split + 1));
        long long parsed_span = 0;
        if (span_text.empty() || !parse_i64(span_text, &parsed_span) || parsed_span <= 0) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "invalid united memory span"};
        }
        if (parsed_span > 65536) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "united memory span exceeds 64KB"};
        }
        span = static_cast<std::size_t>(parsed_span);
        source = trim(source.substr(0, span_split));
    }

    if (source.find(';') == std::string::npos) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "united memory value needs multiple terms"};
    }

    std::vector<SearchTerm> terms;
    std::size_t offset = 0;
    while (offset <= source.size()) {
        const std::size_t split = source.find(';', offset);
        std::string token = trim(source.substr(
            offset,
            split == std::string::npos ? std::string::npos : split - offset
        ));
        if (token.empty()) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "empty united memory term"};
        }

        int type = default_type;
        const int explicit_type = suffix_type(token.back());
        if (explicit_type != 0) {
            type = explicit_type;
            token.pop_back();
            token = trim(token);
            if (token.empty()) {
                return RuntimeResult{RuntimeStatus::InvalidArgument, "empty united memory typed term"};
            }
        }

        SearchTerm term;
        const RuntimeResult parsed = token.find('~') == std::string::npos
            ? parse_search(token.c_str(), type, &term)
            : parse_range(token.c_str(), type, &term);
        if (!parsed.ok()) {
            return parsed;
        }
        terms.push_back(term);

        if (split == std::string::npos) {
            break;
        }
        offset = split + 1;
    }

    if (terms.size() < 2) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "united memory value needs multiple terms"};
    }

    UnitedSearch search;
    search.terms = std::move(terms);
    search.span = span;
    *out = std::move(search);
    return RuntimeResult{};
}

// 判断内存数据是否匹配搜索条件
bool matches_value(const void* data, const SearchTerm& term) {
    if (data == nullptr || !is_type_id(term.type)) {
        return false;
    }

    if (!term.range) {
        return std::memcmp(data, term.value.bytes.data(), term.value.size) == 0;
    }

    const long double value = number_from(data, term.type);
    return value >= term.bounds.min.number && value <= term.bounds.max.number;
}

// 将内存数据格式化为文本
std::string format_value(const void* data, int type) {
    if (data == nullptr || !is_type_id(type)) {
        return {};
    }

    std::ostringstream stream;
    switch (type) {
        case kTypeDword:
            stream << read_scalar<std::int32_t>(data);
            break;
        case kTypeFloat:
            stream << read_scalar<float>(data);
            break;
        case kTypeDouble:
            stream << read_scalar<double>(data);
            break;
        case kTypeWord:
            stream << read_scalar<std::int16_t>(data);
            break;
        case kTypeByte:
            stream << static_cast<int>(read_scalar<std::int8_t>(data));
            break;
        case kTypeQword:
            stream << read_scalar<std::int64_t>(data);
            break;
        default:
            break;
    }
    return stream.str();
}

} // namespace memory
} // namespace runtime
} // namespace nyx
