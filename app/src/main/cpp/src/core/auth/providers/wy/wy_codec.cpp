#include "src/core/auth/providers/wy/wy_codec.h"

#include "sdk/include/crypto.h"
#include "src/utils/string/hex.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

namespace {

namespace text = ::nyx::utils::string;

// 十六进制编码
std::string hex_encode(const std::string& text) {
    return ::nyx::utils::string::hex(text);
}

// 十六进制解码，非法输入返回空字符串
std::string hex_decode(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    if (!::nyx::utils::string::parse_hex(text, &bytes)) {
        return {};
    }
    if (bytes.empty()) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// 校验自定义 Base64 字母表，前 64 位不能重复或包含填充符
bool valid_alphabet(const std::string& alphabet) {
    if (alphabet.size() < 64) {
        return false;
    }

    std::array<bool, 256> seen{};
    for (std::size_t i = 0; i < 64; ++i) {
        const auto c = static_cast<unsigned char>(alphabet[i]);
        if (alphabet[i] == '=' || seen[c]) {
            return false;
        }
        seen[c] = true;
    }
    return true;
}

// 使用自定义字母表做 Base64 编码
std::string custom_base64_encode(const std::string& text, const std::string& alphabet) {
    if (!valid_alphabet(alphabet)) {
        return {};
    }

    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : text) {
        val = (val << 8) | c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(alphabet[(val >> valb) & 0x3f]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3f]);
    }
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

// 使用自定义字母表做 Base64 解码
std::string custom_base64_decode(const std::string& text, const std::string& alphabet) {
    if (!valid_alphabet(alphabet)) {
        return {};
    }

    std::array<int, 256> table{};
    table.fill(-1);
    for (std::size_t i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    }

    std::string out;
    int val = 0;
    int valb = -8;
    for (char c : text) {
        if (c == '=') {
            break;
        }

        const int idx = table[static_cast<unsigned char>(c)];
        if (idx < 0) {
            return {};
        }

        val = (val << 6) | idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xff));
            valb -= 8;
        }
    }
    return out;
}

// RC4 加解密共用同一流程
std::string rc4_crypt(std::string text, const std::string& key) {
    if (key.empty() || text.empty()) {
        return text;
    }

    sdk::crypt::Rc4(
        reinterpret_cast<const std::uint8_t*>(key.data()),
        key.size(),
        reinterpret_cast<std::uint8_t*>(text.data()),
        text.size()
    );
    return text;
}

// 执行单个编码步骤
std::string apply_encode_step(const Profile& profile, std::string text, CodecStep step) {
    switch (step) {
        case CodecStep::Rc4:
            return rc4_crypt(std::move(text), profile.codec.rc4_key);
        case CodecStep::Hex:
            return hex_encode(text);
        case CodecStep::DefBase:
            return custom_base64_encode(text, profile.codec.alphabet);
    }

    return {};
}

// 执行单个解码步骤
std::string apply_decode_step(const Profile& profile, std::string text, CodecStep step) {
    switch (step) {
        case CodecStep::Rc4:
            return rc4_crypt(std::move(text), profile.codec.rc4_key);
        case CodecStep::Hex:
            return hex_decode(text);
        case CodecStep::DefBase:
            return custom_base64_decode(text, profile.codec.alphabet);
    }

    return {};
}

// 计算 MD5 或 SHA-256 十六进制摘要
std::string digest_hex(const std::uint8_t* data, std::size_t len, bool sha256) {
    if (sha256) {
        std::array<std::uint8_t, 32> digest{};
        sdk::crypt::Sha256Raw(data, len, digest.data());
        return text::hex(digest.data(), digest.size());
    }

    std::array<std::uint8_t, 16> digest{};
    sdk::crypt::Md5Raw(data, len, digest.data());
    return text::hex(digest.data(), digest.size());
}

} // namespace

// 计算 MD5 十六进制摘要
std::string md5_hex(const std::string& text) {
    return digest_hex(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), false);
}

// 计算 SHA-256 十六进制摘要
std::string sha256_hex(const std::string& text) {
    return digest_hex(reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), true);
}

// 按 profile 配置编码 WY 请求体
std::string encode(const Profile& profile, const std::string& plain) {
    const Profile next = with_defaults(profile);
    std::string text = plain;
    for (const auto step : next.codec.request_steps) {
        text = apply_encode_step(next, std::move(text), step);
        if (text.empty() && !plain.empty()) {
            return {};
        }
    }
    return text;
}

// 按 profile 配置解码 WY 响应体
std::string decode(const Profile& profile, const std::string& encoded) {
    const Profile next = with_defaults(profile);
    std::string text = encoded;
    for (const auto step : next.codec.response_steps) {
        text = apply_decode_step(next, std::move(text), step);
        if (text.empty() && !encoded.empty()) {
            return {};
        }
    }
    return text;
}

} // namespace wy
} // namespace auth
} // namespace core
} // namespace nyx
