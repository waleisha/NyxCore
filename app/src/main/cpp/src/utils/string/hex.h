#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nyx {
namespace utils {
namespace string {

enum class HexCase {
    Lower,
    Upper,
};

std::string hex(const void* data, std::size_t len, HexCase letter_case = HexCase::Lower);
std::string hex(std::string_view text, HexCase letter_case = HexCase::Lower);
bool parse_hex(std::string_view text, std::vector<std::uint8_t>* out);
std::string hex_dump(const void* data, std::size_t len, std::size_t bytes_per_line = 16);
std::string hex_dump(std::string_view text, std::size_t bytes_per_line = 16);

} // namespace string
} // namespace utils
} // namespace nyx
