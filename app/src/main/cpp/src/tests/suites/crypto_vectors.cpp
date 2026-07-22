#include "sdk/include/test.h"

#include "sdk/include/crypto.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nyx {
namespace sdk {
namespace test {

namespace {

template <std::size_t N>
bool expect_bytes(
    const char* name,
    const std::array<std::uint8_t, N>& actual,
    const std::array<std::uint8_t, N>& expected
) {
    if (actual == expected) {
        NYX_LOGI("crypto vector passed: %s", name);
        return true;
    }

    const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
    const auto offset = static_cast<std::size_t>(mismatch.first - actual.begin());
    NYX_LOGE(
        "crypto vector failed: %s offset=%zu got=%02x expected=%02x",
        name,
        offset,
        static_cast<unsigned int>(*mismatch.first),
        static_cast<unsigned int>(*mismatch.second)
    );
    return false;
}

bool expect_string(const char* name, const std::string& actual, const std::string& expected) {
    if (actual == expected) {
        NYX_LOGI("crypto vector passed: %s", name);
        return true;
    }

    NYX_LOGE("crypto vector failed: %s got=%s expected=%s", name, actual.c_str(), expected.c_str());
    return false;
}

bool expect_true(const char* name, bool value) {
    if (value) {
        NYX_LOGI("crypto vector passed: %s", name);
        return true;
    }

    NYX_LOGE("crypto vector failed: %s expected true", name);
    return false;
}

template <std::size_t N>
bool expect_vector(
    const char* name,
    const std::vector<std::uint8_t>& actual,
    const std::array<std::uint8_t, N>& expected
) {
    if (actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin())) {
        NYX_LOGI("crypto vector passed: %s", name);
        return true;
    }

    NYX_LOGE("crypto vector failed: %s size=%zu expected=%zu", name, actual.size(), expected.size());
    return false;
}

bool check_sha256() {
    constexpr std::array<std::uint8_t, 3> input{0x61, 0x62, 0x63};
    constexpr std::array<std::uint8_t, 32> expected{
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    std::array<std::uint8_t, 32> actual{};
    crypt::Sha256Raw(input.data(), input.size(), actual.data());
    return expect_bytes("SHA256", actual, expected) &&
           expect_string(
               "SHA256 hex",
               crypt::Sha256("abc"),
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
           ) &&
           expect_vector("SHA256 bytes", crypt::Sha256Bytes("abc"), expected);
}

bool check_blake2b() {
    constexpr std::array<std::uint8_t, 3> input{0x61, 0x62, 0x63};
    constexpr std::array<std::uint8_t, 64> expected{
        0xba, 0x80, 0xa5, 0x3f, 0x98, 0x1c, 0x4d, 0x0d,
        0x6a, 0x27, 0x97, 0xb6, 0x9f, 0x12, 0xf6, 0xe9,
        0x4c, 0x21, 0x2f, 0x14, 0x68, 0x5a, 0xc4, 0xb7,
        0x4b, 0x12, 0xbb, 0x6f, 0xdb, 0xff, 0xa2, 0xd1,
        0x7d, 0x87, 0xc5, 0x39, 0x2a, 0xab, 0x79, 0x2d,
        0xc2, 0x52, 0xd5, 0xde, 0x45, 0x33, 0xcc, 0x95,
        0x18, 0xd3, 0x8a, 0xa8, 0xdb, 0xf1, 0x92, 0x5a,
        0xb9, 0x23, 0x86, 0xed, 0xd4, 0x00, 0x99, 0x23,
    };
    std::array<std::uint8_t, 64> actual{};
    crypt::Blake2bRaw(input.data(), input.size(), actual.data());
    return expect_bytes("BLAKE2b", actual, expected) &&
           expect_string(
               "BLAKE2b hex",
               crypt::Blake2b("abc"),
               "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
           ) &&
           expect_vector("BLAKE2b bytes", crypt::Blake2bBytes("abc"), expected);
}

bool check_aes_256_gcm() {
    constexpr std::array<std::uint8_t, 32> key{};
    constexpr std::array<std::uint8_t, 12> iv{};
    constexpr std::array<std::uint8_t, 16> mac{
        0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0,
        0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19,
    };
    std::array<std::uint8_t, 16> ciphertext{
        0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e,
        0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18,
    };
    constexpr std::array<std::uint8_t, 16> expected{};
    std::array<std::uint8_t, 16> actual{};

    if (!crypt::AesDecryptRaw(ciphertext.data(), ciphertext.size(), key.data(), iv.data(), mac.data(), actual.data())) {
        NYX_LOGE("crypto vector failed: AES-256-GCM decrypt rejected vector");
        return false;
    }

    auto plain = crypt::AesDecrypt(
        std::vector<std::uint8_t>(ciphertext.begin(), ciphertext.end()),
        std::vector<std::uint8_t>(key.begin(), key.end()),
        std::vector<std::uint8_t>(iv.begin(), iv.end()),
        std::vector<std::uint8_t>(mac.begin(), mac.end())
    );

    return expect_bytes("AES-256-GCM", actual, expected) &&
           expect_true("AES-256-GCM vector decrypt succeeds", plain.ok()) &&
           expect_vector("AES-256-GCM vector plaintext", plain.value, expected);
}

bool check_md5() {
    constexpr std::array<std::uint8_t, 3> input{0x61, 0x62, 0x63};
    constexpr std::array<std::uint8_t, 16> expected{
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72,
    };
    std::array<std::uint8_t, 16> actual{};
    crypt::Md5Raw(input.data(), input.size(), actual.data());
    return expect_bytes("MD5", actual, expected) &&
           expect_string("MD5 hex", crypt::Md5("abc"), "900150983cd24fb0d6963f7d28e17f72") &&
           expect_vector("MD5 bytes", crypt::Md5Bytes("abc"), expected);
}

bool check_rc4() {
    constexpr std::array<std::uint8_t, 3> key{0x4b, 0x65, 0x79};
    std::array<std::uint8_t, 9> actual{0x50, 0x6c, 0x61, 0x69, 0x6e, 0x74, 0x65, 0x78, 0x74};
    constexpr std::array<std::uint8_t, 9> expected{0xbb, 0xf3, 0x16, 0xe8, 0xd9, 0x40, 0xaf, 0x0a, 0xd3};
    crypt::Rc4(key.data(), key.size(), actual.data(), actual.size());
    return expect_bytes("RC4", actual, expected);
}

bool check_xchacha20_poly1305() {
    std::array<std::uint8_t, 32> key{};
    std::array<std::uint8_t, 24> nonce{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t i = 0; i < nonce.size(); ++i) {
        nonce[i] = static_cast<std::uint8_t>(0xa0 + i);
    }

    std::array<std::uint8_t, 3> payload{0x6e, 0x79, 0x78};
    std::array<std::uint8_t, 16> mac{};
    constexpr std::array<std::uint8_t, 3> expected_payload{0x2b, 0x95, 0x72};
    constexpr std::array<std::uint8_t, 16> expected_mac{
        0x4f, 0x34, 0x7c, 0xa6, 0x1b, 0xe6, 0xa4, 0xbc,
        0x5e, 0x4e, 0x4e, 0x1c, 0xf5, 0xef, 0xdc, 0xa1,
    };

    crypt::ChaCha20(payload.data(), payload.size(), key.data(), nonce.data(), mac.data());
    return expect_bytes("XChaCha20-Poly1305 ciphertext", payload, expected_payload) &&
           expect_bytes("XChaCha20-Poly1305 mac", mac, expected_mac);
}

} // namespace

bool CheckCrypto() {
    bool ok = true;
    ok = check_sha256() && ok;
    ok = check_blake2b() && ok;
    ok = check_aes_256_gcm() && ok;
    ok = check_md5() && ok;
    ok = check_rc4() && ok;
    ok = check_xchacha20_poly1305() && ok;

    NYX_LOGI("crypto vectors %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
