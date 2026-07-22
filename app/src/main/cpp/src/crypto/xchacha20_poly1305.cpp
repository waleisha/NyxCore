#include "sdk/include/crypto.h"

#include "monocypher.h"

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class XChaCha20Poly1305 {
public:
    static void encrypt(
        uint8_t* payload,
        size_t len,
        const uint8_t key[32],
        const uint8_t nonce[24],
        uint8_t mac[16]
    ) {
        if ((payload == nullptr && len > 0) || key == nullptr || nonce == nullptr || mac == nullptr) {
            NYX_LOGE("ChaCha20 received invalid buffer");
            return;
        }

        crypto_aead_lock(payload, mac, key, nonce, nullptr, 0, bytes_or_empty(payload, len), len);
    }

private:
    static const uint8_t* bytes_or_empty(const uint8_t* data, size_t len) {
        static const uint8_t empty = 0;
        return data != nullptr || len > 0 ? data : &empty;
    }
};

} // namespace

void ChaCha20(
    uint8_t* payload,
    size_t len,
    const uint8_t key[32],
    const uint8_t nonce[24],
    uint8_t mac[16]
) {
    XChaCha20Poly1305::encrypt(payload, len, key, nonce, mac);
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
