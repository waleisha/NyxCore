#include "src/utils/json_utils.h"

#include <limits>

namespace nyx {
namespace utils {
namespace json {

const nlohmann::json* field(const nlohmann::json& root, const char* key) {
    if (!root.is_object() || key == nullptr) {
        return nullptr;
    }

    const auto it = root.find(key);
    return it == root.end() ? nullptr : &(*it);
}

bool read_string(const nlohmann::json& root, const char* key, std::string* out) {
    const auto* value = field(root, key);
    if (value == nullptr || !value->is_string()) {
        return false;
    }

    if (out != nullptr) {
        *out = value->get<std::string>();
    }
    return true;
}

bool read_int64(const nlohmann::json& root, const char* key, std::int64_t* out) {
    const auto* value = field(root, key);
    if (value == nullptr) {
        return false;
    }

    try {
        if (value->is_number_integer() || value->is_number_unsigned()) {
            const auto number = value->get<std::int64_t>();
            if (out != nullptr) {
                *out = number;
            }
            return true;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }

    return false;
}

bool read_int(const nlohmann::json& root, const char* key, int* out) {
    std::int64_t value = 0;
    if (!read_int64(root, key, &value) ||
        value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        return false;
    }

    if (out != nullptr) {
        *out = static_cast<int>(value);
    }
    return true;
}

bool read_bool(const nlohmann::json& root, const char* key, bool* out) {
    const auto* value = field(root, key);
    if (value == nullptr || !value->is_boolean()) {
        return false;
    }

    if (out != nullptr) {
        *out = value->get<bool>();
    }
    return true;
}

const nlohmann::json* read_array(const nlohmann::json& root, const char* key) {
    const auto* value = field(root, key);
    return value != nullptr && value->is_array() ? value : nullptr;
}

} // namespace json
} // namespace utils
} // namespace nyx
