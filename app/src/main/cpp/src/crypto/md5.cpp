#include "sdk/include/crypto.h"

#include "src/utils/string/hex.h"

#include "mbedtls/md5.h"

#include <vector>

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class Md5Digest {
public:
    static void digest(const uint8_t* data, size_t len, uint8_t out[16]) {
        if (out == nullptr || (data == nullptr && len > 0)) {
            NYX_LOGE("Md5 received invalid buffer");
            return;
        }

        const auto result = mbedtls_md5(bytes_or_empty(data, len), len, out);
        if (result != 0) {
            NYX_LOGE("Md5 failed: %d", result);
        }
    }

private:
    static const uint8_t* bytes_or_empty(const uint8_t* data, size_t len) {
        static const uint8_t empty = 0;
        return data != nullptr || len > 0 ? data : &empty;
    }
};

} // namespace

void Md5Raw(const uint8_t* data, size_t len, uint8_t out[16]) {
    Md5Digest::digest(data, len, out);
}

std::vector<std::uint8_t> Md5Bytes(std::string_view data) {
    std::vector<std::uint8_t> out(16);
    Md5Digest::digest(
        reinterpret_cast<const std::uint8_t*>(data.data()),
        data.size(),
        out.data()
    );
    return out;
}

std::string Md5(std::string_view data) {
    const auto digest = Md5Bytes(data);
    return ::nyx::utils::string::hex(digest.data(), digest.size());
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
