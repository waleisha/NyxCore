#include "src/utils/string/format.h"

#include <cstdio>
#include <vector>

namespace nyx {
namespace utils {
namespace string {

std::string vformat(const char* format, va_list args) {
    if (format == nullptr) {
        return {};
    }

    va_list size_args;
    va_copy(size_args, args);
    const int needed = std::vsnprintf(nullptr, 0, format, size_args);
    va_end(size_args);
    if (needed < 0) {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(needed) + 1);
    va_list write_args;
    va_copy(write_args, args);
    const int written = std::vsnprintf(buffer.data(), buffer.size(), format, write_args);
    va_end(write_args);
    if (written < 0) {
        return {};
    }

    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

std::string format(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::string out = vformat(format, args);
    va_end(args);
    return out;
}

bool format_to(char* out, std::size_t out_len, const char* format, ...) {
    if (out == nullptr || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    if (format == nullptr) {
        return false;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(out, out_len, format, args);
    va_end(args);

    out[out_len - 1] = '\0';
    return written >= 0 && static_cast<std::size_t>(written) < out_len;
}

} // namespace string
} // namespace utils
} // namespace nyx
