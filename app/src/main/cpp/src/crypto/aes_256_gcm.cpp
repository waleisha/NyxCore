#include "sdk/include/crypto.h"

#include "mbedtls/gcm.h"

#include <vector>

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class Aes256Gcm {
public:
    static bool decrypt(
        const uint8_t* ciphertext,
        size_t len,
        const uint8_t key[32],
        const uint8_t iv[12],
        const uint8_t mac[16],
        uint8_t* plaintext
    ) {
        if ((ciphertext == nullptr && len > 0) || key == nullptr || iv == nullptr || mac == nullptr ||
            (plaintext == nullptr && len > 0)) {
            NYX_LOGE("AesDecrypt received invalid buffer");
            return false;
        }

        mbedtls_gcm_context ctx;
        mbedtls_gcm_init(&ctx);

        const auto set_key_result = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);
        if (set_key_result != 0) {
            NYX_LOGE("AesDecrypt setkey failed: %d", set_key_result);
            mbedtls_gcm_free(&ctx);
            return false;
        }

        const auto decrypt_result = mbedtls_gcm_auth_decrypt(
            &ctx,
            len,
            iv,
            12,
            nullptr,
            0,
            mac,
            16,
            bytes_or_empty(ciphertext, len),
            plaintext
        );
        mbedtls_gcm_free(&ctx);

        if (decrypt_result != 0) {
            NYX_LOGW("AesDecrypt authentication failed: %d", decrypt_result);
            return false;
        }

        return true;
    }

private:
    static const uint8_t* bytes_or_empty(const uint8_t* data, size_t len) {
        static const uint8_t empty = 0;
        return data != nullptr || len > 0 ? data : &empty;
    }
};

} // namespace

bool AesDecryptRaw(
    const uint8_t* ciphertext,
    size_t len,
    const uint8_t key[32],
    const uint8_t iv[12],
    const uint8_t mac[16],
    uint8_t* plaintext
) {
    return Aes256Gcm::decrypt(ciphertext, len, key, iv, mac, plaintext);
}

Value<std::vector<std::uint8_t>> AesDecrypt(
    const std::vector<std::uint8_t>& cipher,
    const std::vector<std::uint8_t>& key,
    const std::vector<std::uint8_t>& iv,
    const std::vector<std::uint8_t>& mac
) {
    Value<std::vector<std::uint8_t>> out;
    if (key.size() != 32 || iv.size() != 12 || mac.size() != 16) {
        out.result = Result{Status::InvalidArgument, "AES-256-GCM key, iv, or mac has invalid size"};
        return out;
    }

    out.value.resize(cipher.size());
    const bool ok = AesDecryptRaw(
        cipher.data(),
        cipher.size(),
        key.data(),
        iv.data(),
        mac.data(),
        out.value.data()
    );
    if (!ok) {
        out.value.clear();
        out.result = Result{Status::Failed, "AES-256-GCM decrypt failed"};
        return out;
    }

    out.result = Result{};
    return out;
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
