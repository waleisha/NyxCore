#include "src/utils/string/utf8.h"

namespace nyx {
namespace utils {
namespace string {

namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

bool is_cont(unsigned char value) {
    return (value & 0xc0U) == 0x80U;
}

bool in_range(unsigned char value, unsigned char min, unsigned char max) {
    return value >= min && value <= max;
}

std::size_t valid_len(std::string_view text, std::size_t pos) {
    const auto first = static_cast<unsigned char>(text[pos]);
    const std::size_t left = text.size() - pos;

    if (first <= 0x7fU) {
        return 1;
    }

    if (in_range(first, 0xc2U, 0xdfU)) {
        return left >= 2 && is_cont(static_cast<unsigned char>(text[pos + 1])) ? 2 : 0;
    }

    if (first == 0xe0U) {
        return left >= 3 &&
                in_range(static_cast<unsigned char>(text[pos + 1]), 0xa0U, 0xbfU) &&
                is_cont(static_cast<unsigned char>(text[pos + 2]))
            ? 3
            : 0;
    }

    if (in_range(first, 0xe1U, 0xecU) || in_range(first, 0xeeU, 0xefU)) {
        return left >= 3 &&
                is_cont(static_cast<unsigned char>(text[pos + 1])) &&
                is_cont(static_cast<unsigned char>(text[pos + 2]))
            ? 3
            : 0;
    }

    if (first == 0xedU) {
        return left >= 3 &&
                in_range(static_cast<unsigned char>(text[pos + 1]), 0x80U, 0x9fU) &&
                is_cont(static_cast<unsigned char>(text[pos + 2]))
            ? 3
            : 0;
    }

    if (first == 0xf0U) {
        return left >= 4 &&
                in_range(static_cast<unsigned char>(text[pos + 1]), 0x90U, 0xbfU) &&
                is_cont(static_cast<unsigned char>(text[pos + 2])) &&
                is_cont(static_cast<unsigned char>(text[pos + 3]))
            ? 4
            : 0;
    }

    if (in_range(first, 0xf1U, 0xf3U)) {
        return left >= 4 &&
                is_cont(static_cast<unsigned char>(text[pos + 1])) &&
                is_cont(static_cast<unsigned char>(text[pos + 2])) &&
                is_cont(static_cast<unsigned char>(text[pos + 3]))
            ? 4
            : 0;
    }

    if (first == 0xf4U) {
        return left >= 4 &&
                in_range(static_cast<unsigned char>(text[pos + 1]), 0x80U, 0x8fU) &&
                is_cont(static_cast<unsigned char>(text[pos + 2])) &&
                is_cont(static_cast<unsigned char>(text[pos + 3]))
            ? 4
            : 0;
    }

    return 0;
}

} // namespace

bool is_utf8(std::string_view text) {
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t len = valid_len(text, pos);
        if (len == 0) {
            return false;
        }
        pos += len;
    }
    return true;
}

std::size_t utf8_length(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t len = valid_len(text, pos);
        if (len == 0) {
            return std::string_view::npos;
        }
        pos += len;
        ++count;
    }
    return count;
}

std::string clean_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t len = valid_len(text, pos);
        if (len == 0) {
            out.append(kReplacement);
            ++pos;
            continue;
        }

        out.append(text.substr(pos, len));
        pos += len;
    }

    return out;
}

} // namespace string
} // namespace utils
} // namespace nyx
