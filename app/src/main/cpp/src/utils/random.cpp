#include "src/utils/random.h"

#include <fstream>
#include <utility>

namespace nyx {
namespace utils {

bool random_bytes(std::size_t len, std::vector<std::uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }

    std::vector<std::uint8_t> bytes(len);
    std::ifstream source("/dev/urandom", std::ios::binary);
    if (!source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return false;
    }

    *out = std::move(bytes);
    return true;
}

} // namespace utils
} // namespace nyx
