#include "src/core/auth/auth_store.h"

#include "src/utils/json_utils.h"
#include "src/utils/random.h"
#include "src/utils/string/hex.h"

#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "nlohmann/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace nyx {
namespace core {
namespace auth {

namespace {

// 会话封存文件版本
constexpr int kSessionSealVersion = 1;
// AES-GCM IV 长度
constexpr std::size_t kGcmIvLen = 12;
// AES-GCM Tag 长度
constexpr std::size_t kGcmTagLen = 16;

namespace text = ::nyx::utils::string;
namespace json = ::nyx::utils::json;

// 派生会话封存密钥，绑定上下文指纹和设备 ID
bool session_key(std::string_view context, std::string_view device_id, std::uint8_t out[32]) {
    if (out == nullptr || context.empty() || device_id.empty()) {
        return false;
    }

    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return false;
    }

    const std::string ikm = "nyx-auth-store-ikm-v1\n" + std::string(context) + "\n" + std::string(device_id);
    const std::string salt = "nyx-auth-store-salt-v1\n" + std::string(context);
    const std::string label = "nyx-auth-session-v1";
    return mbedtls_hkdf(
        info,
        reinterpret_cast<const unsigned char*>(salt.data()),
        salt.size(),
        reinterpret_cast<const unsigned char*>(ikm.data()),
        ikm.size(),
        reinterpret_cast<const unsigned char*>(label.data()),
        label.size(),
        out,
        32
    ) == 0;
}

// 使用 AES-GCM 封存明文会话
bool seal(
    std::string_view key_context,
    std::string_view device_id,
    std::string_view aad,
    std::string_view plain,
    std::vector<std::uint8_t>* iv,
    std::vector<std::uint8_t>* tag,
    std::vector<std::uint8_t>* data
) {
    if (iv == nullptr || tag == nullptr || data == nullptr || key_context.empty() || device_id.empty()) {
        return false;
    }

    std::array<std::uint8_t, 32> key{};
    if (!session_key(key_context, device_id, key.data())) {
        return false;
    }

    std::vector<std::uint8_t> next_iv;
    if (!::nyx::utils::random_bytes(kGcmIvLen, &next_iv)) {
        return false;
    }

    std::vector<std::uint8_t> next_data(plain.size());
    std::vector<std::uint8_t> next_tag(kGcmTagLen);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    const int set_key_result = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (set_key_result != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    const int crypt_result = mbedtls_gcm_crypt_and_tag(
        &ctx,
        MBEDTLS_GCM_ENCRYPT,
        plain.size(),
        next_iv.data(),
        next_iv.size(),
        aad.empty() ? nullptr : reinterpret_cast<const unsigned char*>(aad.data()),
        aad.size(),
        plain.empty() ? nullptr : reinterpret_cast<const unsigned char*>(plain.data()),
        next_data.empty() ? nullptr : next_data.data(),
        next_tag.size(),
        next_tag.data()
    );
    mbedtls_gcm_free(&ctx);
    if (crypt_result != 0) {
        return false;
    }

    *iv = std::move(next_iv);
    *tag = std::move(next_tag);
    *data = std::move(next_data);
    return true;
}

// 解开 AES-GCM 会话封存，认证失败时不返回明文
bool unseal(
    std::string_view key_context,
    std::string_view device_id,
    std::string_view aad,
    const std::vector<std::uint8_t>& iv,
    const std::vector<std::uint8_t>& tag,
    const std::vector<std::uint8_t>& data,
    std::string* out
) {
    if (out == nullptr || key_context.empty() || device_id.empty() ||
        iv.size() != kGcmIvLen || tag.size() != kGcmTagLen) {
        return false;
    }

    std::array<std::uint8_t, 32> key{};
    if (!session_key(key_context, device_id, key.data())) {
        return false;
    }

    std::vector<std::uint8_t> plain(data.size());
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    const int set_key_result = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key.data(), 256);
    if (set_key_result != 0) {
        mbedtls_gcm_free(&ctx);
        return false;
    }

    const int crypt_result = mbedtls_gcm_auth_decrypt(
        &ctx,
        data.size(),
        iv.data(),
        iv.size(),
        aad.empty() ? nullptr : reinterpret_cast<const unsigned char*>(aad.data()),
        aad.size(),
        tag.data(),
        tag.size(),
        data.empty() ? nullptr : data.data(),
        plain.empty() ? nullptr : plain.data()
    );
    mbedtls_gcm_free(&ctx);
    if (crypt_result != 0) {
        return false;
    }

    out->assign(reinterpret_cast<const char*>(plain.data()), plain.size());
    return true;
}

// 附加认证数据，确保会话只属于当前上下文、设备和 provider
std::string aad(const ContextState& context, const std::string& provider, const std::string& device_id) {
    return "nyx-auth-session-v1\n" +
        context.fingerprint() + "\n" +
        device_id + "\n" +
        provider;
}

// 会话封存文件路径
std::filesystem::path store_path(const ContextState& context) {
    return std::filesystem::path(context.store_dir()) / "session.seal";
}

// 写入临时文件后替换正式文件，避免半写入记录生效
bool write_sealed_file(const std::filesystem::path& path, const nlohmann::json& root) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << root.dump();
        out.flush();
        if (!out) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    // Android/POSIX 上 rename 会原子替换旧文件
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (!error) {
        return true;
    }
    std::filesystem::remove(temporary, error);
    return false;
}

// 解码会话封存外层 JSON
bool decode_sealed(const nlohmann::json& root, std::vector<std::uint8_t>* iv, std::vector<std::uint8_t>* tag, std::vector<std::uint8_t>* data) {
    int version = 0;
    std::string iv_hex;
    std::string tag_hex;
    std::string data_hex;
    if (iv == nullptr || tag == nullptr || data == nullptr ||
        !root.is_object() ||
        !json::read_int(root, "version", &version) ||
        version != kSessionSealVersion ||
        !json::read_string(root, "iv", &iv_hex) ||
        !json::read_string(root, "tag", &tag_hex) ||
        !json::read_string(root, "data", &data_hex)) {
        return false;
    }

    return text::parse_hex(iv_hex, iv) &&
        text::parse_hex(tag_hex, tag) &&
        text::parse_hex(data_hex, data);
}

// 编码会话封存外层 JSON
nlohmann::json encode_sealed(const std::vector<std::uint8_t>& iv, const std::vector<std::uint8_t>& tag, const std::vector<std::uint8_t>& data) {
    nlohmann::json root;
    root["version"] = kSessionSealVersion;
    root["iv"] = text::hex(iv.data(), iv.size());
    root["tag"] = text::hex(tag.data(), tag.size());
    root["data"] = text::hex(data.data(), data.size());
    return root;
}

// 将会话状态序列化为明文 JSON
nlohmann::json session_json(const std::string& provider, const SessionState& session) {
    nlohmann::json root;
    root["version"] = 1;
    root["provider"] = provider;
    root["license"] = session.license;
    root["device_id"] = session.device_id;
    root["token"] = session.token;
    root["expires_at"] = session.expires_at;
    root["remaining_uses"] = session.remaining_uses;
    root["heartbeat_failures"] = session.heartbeat_failures;

    auto features = nlohmann::json::array();
    for (const auto& ticket : session.features) {
        nlohmann::json item;
        item["feature"] = ticket.feature;
        item["allowed"] = ticket.allowed;
        item["expires_at"] = ticket.expires_at;
        features.push_back(std::move(item));
    }
    root["features"] = std::move(features);
    return root;
}

// 解析会话 JSON，并校验 provider、设备 ID 和过期时间
bool parse_session(const nlohmann::json& root, const std::string& provider, const std::string& device_id, SessionState* session) {
    int version = 0;
    std::string parsed_provider;
    std::string parsed_device;
    std::string license;
    std::string token;
    std::int64_t expires_at = 0;
    int remaining_uses = 0;
    int heartbeat_failures = 0;
    const auto* features = json::read_array(root, "features");
    if (session == nullptr ||
        !root.is_object() ||
        !json::read_int(root, "version", &version) ||
        version != 1 ||
        !json::read_string(root, "provider", &parsed_provider) ||
        parsed_provider != provider ||
        !json::read_string(root, "device_id", &parsed_device) ||
        parsed_device != device_id ||
        !json::read_string(root, "license", &license) ||
        !json::read_string(root, "token", &token) ||
        !json::read_int64(root, "expires_at", &expires_at) ||
        !json::read_int(root, "remaining_uses", &remaining_uses) ||
        !json::read_int(root, "heartbeat_failures", &heartbeat_failures) ||
        features == nullptr) {
        return false;
    }

    SessionState next;
    next.active = true;
    next.license = std::move(license);
    next.device_id = std::move(parsed_device);
    next.token = std::move(token);
    next.expires_at = expires_at;
    next.remaining_uses = remaining_uses;
    next.heartbeat_failures = heartbeat_failures;

    for (const auto& item : *features) {
        std::string feature;
        bool allowed = false;
        std::int64_t feature_expires_at = 0;
        if (!item.is_object() ||
            !json::read_string(item, "feature", &feature) ||
            !json::read_bool(item, "allowed", &allowed) ||
            !json::read_int64(item, "expires_at", &feature_expires_at)) {
            return false;
        }

        FeatureTicket ticket;
        ticket.feature = std::move(feature);
        ticket.allowed = allowed;
        ticket.expires_at = feature_expires_at;
        next.features.push_back(std::move(ticket));
    }

    if (!next.live()) {
        return false;
    }

    *session = std::move(next);
    return true;
}

} // namespace

// 保存当前会话到本地封存文件
bool save_session(const ContextState& context, const std::string& provider, const SessionState& session) {
    if (!context.is_valid() || context.android_id.empty() || provider.empty() || !session.live()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(context.store_dir(), error);
    if (error) {
        return false;
    }

    const std::string plain = session_json(provider, session).dump();
    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> tag;
    std::vector<std::uint8_t> data;
    if (!seal(context.fingerprint(), session.device_id, aad(context, provider, session.device_id), plain, &iv, &tag, &data)) {
        return false;
    }

    return write_sealed_file(store_path(context), encode_sealed(iv, tag, data));
}

// 从本地封存文件恢复会话，损坏或不匹配时清理文件
bool load_session(
    const ContextState& context,
    const std::string& provider,
    const std::string& device_id,
    SessionState* session
) {
    if (!context.is_valid() || context.android_id.empty() || provider.empty() || device_id.empty() || session == nullptr) {
        return false;
    }

    std::ifstream in(store_path(context), std::ios::binary);
    if (!in) {
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception&) {
        clear_session_store(context);
        return false;
    }

    std::vector<std::uint8_t> iv;
    std::vector<std::uint8_t> tag;
    std::vector<std::uint8_t> data;
    std::string plain;
    if (!decode_sealed(root, &iv, &tag, &data) ||
        !unseal(context.fingerprint(), device_id, aad(context, provider, device_id), iv, tag, data, &plain)) {
        clear_session_store(context);
        return false;
    }

    nlohmann::json session_root;
    try {
        session_root = nlohmann::json::parse(plain);
    } catch (const nlohmann::json::exception&) {
        clear_session_store(context);
        return false;
    }

    if (!parse_session(session_root, provider, device_id, session)) {
        clear_session_store(context);
        return false;
    }

    return true;
}

// 清除本地会话封存文件
void clear_session_store(const ContextState& context) {
    std::error_code error;
    std::filesystem::remove(store_path(context), error);
}

// 判断本地会话封存文件是否存在
bool has_session_store(const ContextState& context) {
    std::error_code error;
    return std::filesystem::exists(store_path(context), error);
}

// 获取本地会话封存文件路径
std::string session_store_path(const ContextState& context) {
    return store_path(context).string();
}

} // namespace auth
} // namespace core
} // namespace nyx
