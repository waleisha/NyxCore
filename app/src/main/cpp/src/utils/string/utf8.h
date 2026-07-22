#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace nyx {
namespace utils {
namespace string {

bool is_utf8(std::string_view text);
std::size_t utf8_length(std::string_view text);
std::string clean_utf8(std::string_view text);

} // namespace string
} // namespace utils
} // namespace nyx
