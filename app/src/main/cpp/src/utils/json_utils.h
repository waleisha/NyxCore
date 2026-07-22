#pragma once

#include "nlohmann/json.hpp"

#include <cstdint>
#include <string>

namespace nyx {
namespace utils {
namespace json {

const nlohmann::json* field(const nlohmann::json& root, const char* key);
bool read_string(const nlohmann::json& root, const char* key, std::string* out);
bool read_int64(const nlohmann::json& root, const char* key, std::int64_t* out);
bool read_int(const nlohmann::json& root, const char* key, int* out);
bool read_bool(const nlohmann::json& root, const char* key, bool* out);
const nlohmann::json* read_array(const nlohmann::json& root, const char* key);

} // namespace json
} // namespace utils
} // namespace nyx
