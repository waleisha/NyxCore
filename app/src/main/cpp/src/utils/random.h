#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nyx {
namespace utils {

bool random_bytes(std::size_t len, std::vector<std::uint8_t>* out);

} // namespace utils
} // namespace nyx
