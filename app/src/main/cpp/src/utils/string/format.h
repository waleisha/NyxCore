#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>

namespace nyx {
namespace utils {
namespace string {

std::string vformat(const char* format, va_list args);
std::string format(const char* format, ...);
bool format_to(char* out, std::size_t out_len, const char* format, ...);

} // namespace string
} // namespace utils
} // namespace nyx
