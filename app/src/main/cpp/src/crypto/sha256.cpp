#include "sdk/include/crypto.h"

#include "src/utils/string/hex.h"

#include "mbedtls/sha256.h"

#include <array>
#include <vector>

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class Sha256Digest {
public:
    static void digest(const uint8_t* data, size_t len, uint8_t out[32]) {
        if (out == nullptr || (data == nullptr && len > 0)) {
            NYX_LOGE("Sha256 received invalid buffer");
            return;
        }

        const auto result = mbedtls_sha256(bytes_or_empty(data, len), len, out, 0);
        if (result != 0) {
            NYX_LOGE("Sha256 failed: %d", result);
        }
    }

private:
    static const uint8_t* bytes_or_empty(const uint8_t* data, size_t len) {
        static const uint8_t empty = 0;
        return data != nullptr || len > 0 ? data : &empty;
    }
};

} // namespace

void Sha256Raw(const uint8_t* data, size_t len, uint8_t out[32]) {
    Sha256Digest::digest(data, len, out);
}

std::vector<std::uint8_t> Sha256Bytes(std::string_view data) {
    std::vector<std::uint8_t> out(32);
    Sha256Digest::digest(
        reinterpret_cast<const std::uint8_t*>(data.data()),
        data.size(),
        out.data()
    );
    return out;
}

std::string Sha256(std::string_view data) {
    const auto digest = Sha256Bytes(data);
    return ::nyx::utils::string::hex(digest.data(), digest.size());
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
