#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace crypt {

// 计算 Blake2b 原始摘要，输出 64 字节
NYX_EXPORT void Blake2bRaw(const uint8_t* data, size_t len, uint8_t out[64]);
// 计算 Blake2b，返回十六进制字符串
NYX_EXPORT std::string Blake2b(std::string_view data);
// 计算 Blake2b，返回原始字节
NYX_EXPORT std::vector<std::uint8_t> Blake2bBytes(std::string_view data);
// 使用 XChaCha20-Poly1305 加密数据并回填认证标签
NYX_EXPORT void ChaCha20(uint8_t* payload, size_t len, const uint8_t key[32], const uint8_t nonce[24], uint8_t mac[16]);

// AES-256-GCM 原始解密，认证失败时返回 false
NYX_EXPORT bool AesDecryptRaw(
    const uint8_t* ciphertext,
    size_t len,
    const uint8_t key[32],
    const uint8_t iv[12],
    const uint8_t mac[16],
    uint8_t* plaintext
);
// AES-256-GCM 解密，返回明文字节
NYX_EXPORT Value<std::vector<std::uint8_t>> AesDecrypt(
    const std::vector<std::uint8_t>& cipher,
    const std::vector<std::uint8_t>& key,
    const std::vector<std::uint8_t>& iv,
    const std::vector<std::uint8_t>& mac
);
// 计算 SHA-256 原始摘要，输出 32 字节
NYX_EXPORT void Sha256Raw(const uint8_t* data, size_t len, uint8_t out[32]);
// 计算 SHA-256，返回十六进制字符串
NYX_EXPORT std::string Sha256(std::string_view data);
// 计算 SHA-256，返回原始字节
NYX_EXPORT std::vector<std::uint8_t> Sha256Bytes(std::string_view data);

// 计算 MD5 原始摘要，输出 16 字节
NYX_EXPORT void Md5Raw(const uint8_t* data, size_t len, uint8_t out[16]);
// 计算 MD5，返回十六进制字符串
NYX_EXPORT std::string Md5(std::string_view data);
// 计算 MD5，返回原始字节
NYX_EXPORT std::vector<std::uint8_t> Md5Bytes(std::string_view data);
// 使用 RC4 对数据原地加解密
NYX_EXPORT void Rc4(const uint8_t* key, size_t key_len, uint8_t* data, size_t data_len);

} // namespace crypt
} // namespace sdk
} // namespace nyx
