#include "src/core/auth/auth_device.h"

#include "sdk/include/crypto.h"
#include "src/utils/json_utils.h"
#include "src/utils/random.h"
#include "src/utils/string/hex.h"

#include "mbedtls/md.h"
#include "nlohmann/json.hpp"

#include <array>
#include <algorithm>
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

// 设备 ID 封存文件版本
constexpr int kDeviceSealVersion = 1;

namespace text = ::nyx::utils::string;
namespace json = ::nyx::utils::json;

// 计算 HMAC-SHA256
std::array<std::uint8_t, 32> hmac_sha256(std::string_view key, std::string_view text) {
    std::array<std::uint8_t, 32> out{};
    const auto* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return out;
    }

    mbedtls_md_hmac(
        info,
        reinterpret_cast<const unsigned char*>(key.data()),
        key.size(),
        reinterpret_cast<const unsigned char*>(text.data()),
        text.size(),
        out.data()
    );
    return out;
}

// 常量时间比较，避免 MAC 校验暴露早停位置
bool same_bytes(const std::array<std::uint8_t, 32>& left, const std::array<std::uint8_t, 32>& right) {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff = static_cast<std::uint8_t>(diff | (left[i] ^ right[i]));
    }
    return diff == 0;
}

// 设备 ID 封存文件路径
std::filesystem::path device_path(const ContextState& context) {
    return std::filesystem::path(context.store_dir()) / "device.seal";
}

// 写入临时文件后替换正式文件，避免半写入记录生效
bool write_id_file(const std::filesystem::path& path, const nlohmann::json& root) {
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

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (!error) {
        return true;
    }
    std::filesystem::remove(temporary, error);
    return false;
}

// 清除设备 ID 封存文件
void clear_id_file(const ContextState& context) {
    std::error_code error;
    std::filesystem::remove(device_path(context), error);
}

// 设备 ID MAC 密钥，绑定包名和签名证书
std::string seal_key(const ContextState& context) {
    return ::nyx::sdk::crypt::Sha256(
        "nyx-auth-device-key-v1\n" +
        context.package_name + "\n" +
        context.cert_sha256
    );
}

// 设备 ID MAC 明文
std::string seal_text(const std::string& id, const std::string& binding) {
    return "nyx-auth-device-seal-v1\n" + id + "\n" + binding;
}

// 根据稳定设备字段生成确定性设备 ID
std::string deterministic_id(const ContextState& context) {
    return ::nyx::sdk::crypt::Sha256(
        "nyx-device-v1\n" +
        context.package_name + "\n" +
        context.cert_sha256 + "\n" +
        context.android_id
    );
}

// 生成随机设备 ID
bool random_id(std::string* out) {
    std::vector<std::uint8_t> bytes;
    if (out == nullptr || !::nyx::utils::random_bytes(32, &bytes)) {
        return false;
    }

    *out = text::hex(bytes.data(), bytes.size());
    return true;
}

// 保存设备 ID，并用上下文指纹封存
bool save_id(const ContextState& context, const std::string& id) {
    std::error_code error;
    std::filesystem::create_directories(context.store_dir(), error);
    if (error) {
        return false;
    }

    const std::string binding = context.fingerprint();
    const auto mac = hmac_sha256(seal_key(context), seal_text(id, binding));

    nlohmann::json root;
    root["version"] = kDeviceSealVersion;
    root["id"] = id;
    root["binding"] = binding;
    root["mac"] = text::hex(mac.data(), mac.size());

    return write_id_file(device_path(context), root);
}

// 读取并校验设备 ID，损坏或不匹配时清理封存文件
bool load_id(const ContextState& context, std::string* out) {
    if (out == nullptr) {
        return false;
    }

    std::ifstream in(device_path(context), std::ios::binary);
    if (!in) {
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception&) {
        clear_id_file(context);
        return false;
    }

    int version = 0;
    std::string id;
    std::string binding;
    std::string mac_hex;
    if (!root.is_object() ||
        !json::read_int(root, "version", &version) ||
        version != kDeviceSealVersion ||
        !json::read_string(root, "id", &id) ||
        !json::read_string(root, "binding", &binding) ||
        !json::read_string(root, "mac", &mac_hex)) {
        clear_id_file(context);
        return false;
    }

    const std::string expected_binding = context.fingerprint();
    if (id.empty() || binding != expected_binding) {
        clear_id_file(context);
        return false;
    }

    std::vector<std::uint8_t> stored_mac;
    if (!text::parse_hex(mac_hex, &stored_mac) || stored_mac.size() != 32) {
        clear_id_file(context);
        return false;
    }

    const auto expected = hmac_sha256(seal_key(context), seal_text(id, binding));
    std::array<std::uint8_t, 32> actual{};
    std::copy(stored_mac.begin(), stored_mac.end(), actual.begin());
    if (!same_bytes(actual, expected)) {
        clear_id_file(context);
        return false;
    }

    *out = id;
    return true;
}

} // namespace

// 获取或创建绑定当前上下文的设备 ID
DeviceIdResult ensure_device_id(const ContextState& context) {
    DeviceIdResult result;
    if (!context.is_valid()) {
        result.message = "auth context is not configured";
        return result;
    }

    if (load_id(context, &result.id)) {
        result.success = true;
        return result;
    }

    if (!context.android_id.empty()) {
        result.id = deterministic_id(context);
    } else if (!random_id(&result.id)) {
        result.message = "device id random source is unavailable";
        return result;
    }

    result.fresh = true;
    if (!save_id(context, result.id)) {
        result.message = "device id seal could not be saved";
        return result;
    }

    result.success = true;
    return result;
}

// 清除本地设备 ID 封存文件
void clear_device_id(const ContextState& context) {
    clear_id_file(context);
}

} // namespace auth
} // namespace core
} // namespace nyx
