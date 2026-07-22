#include "sdk/include/crypto.h"

#include "src/utils/string/hex.h"

#include "monocypher.h"

#include <vector>

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class Blake2bDigest {
public:
    static void digest(const uint8_t* data, size_t len, uint8_t out[64]) {
        if (out == nullptr || (data == nullptr && len > 0)) {
            NYX_LOGE("Blake2b received invalid buffer");
            return;
        }

        crypto_blake2b(out, 64, bytes_or_empty(data, len), len);
    }

private:
    static const uint8_t* bytes_or_empty(const uint8_t* data, size_t len) {
        static const uint8_t empty = 0;
        return data != nullptr || len > 0 ? data : &empty;
    }
};

} // namespace

void Blake2bRaw(const uint8_t* data, size_t len, uint8_t out[64]) {
    Blake2bDigest::digest(data, len, out);
}

std::vector<std::uint8_t> Blake2bBytes(std::string_view data) {
    std::vector<std::uint8_t> out(64);
    Blake2bDigest::digest(
        reinterpret_cast<const std::uint8_t*>(data.data()),
        data.size(),
        out.data()
    );
    return out;
}

std::string Blake2b(std::string_view data) {
    const auto digest = Blake2bBytes(data);
    return ::nyx::utils::string::hex(digest.data(), digest.size());
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
