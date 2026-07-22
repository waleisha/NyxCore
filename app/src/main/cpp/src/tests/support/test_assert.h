#pragma once

#include "sdk/include/utils.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace nyx {
namespace sdk {
namespace test {
namespace expect {

inline bool ok(const char* suite, const char* name) {
    NYX_LOGI("%s %s: passed", suite, name);
    return true;
}

inline bool bool_eq(const char* suite, const char* name, bool actual, bool expected) {
    if (actual == expected) {
        return ok(suite, name);
    }

    NYX_LOGE(
        "%s %s: expected %s, got %s",
        suite,
        name,
        expected ? "true" : "false",
        actual ? "true" : "false"
    );
    return false;
}

inline bool true_value(const char* suite, const char* name, bool actual) {
    return bool_eq(suite, name, actual, true);
}

inline bool size_eq(const char* suite, const char* name, std::size_t actual, std::size_t expected) {
    if (actual == expected) {
        return ok(suite, name);
    }

    NYX_LOGE(
        "%s %s: expected %llu, got %llu",
        suite,
        name,
        static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(actual)
    );
    return false;
}

inline bool text_eq(const char* suite, const char* name, const std::string& actual, const std::string& expected) {
    if (actual == expected) {
        return ok(suite, name);
    }

    NYX_LOGE("%s %s: expected %s, got %s", suite, name, expected.c_str(), actual.c_str());
    return false;
}

inline bool text_eq(const char* suite, const char* name, const char* actual, const char* expected) {
    if (actual == nullptr || expected == nullptr) {
        if (actual == expected) {
            return ok(suite, name);
        }

        NYX_LOGE(
            "%s %s: expected %s, got %s",
            suite,
            name,
            expected != nullptr ? expected : "<null>",
            actual != nullptr ? actual : "<null>"
        );
        return false;
    }

    return text_eq(
        suite,
        name,
        std::string(actual),
        std::string(expected)
    );
}

inline bool contains(const char* suite, const char* name, const std::string& actual, std::string_view expected) {
    if (actual.find(expected) != std::string::npos) {
        return ok(suite, name);
    }

    NYX_LOGE("%s %s: expected fragment missing", suite, name);
    return false;
}

inline bool not_empty(const char* suite, const char* name, const std::string& actual) {
    if (!actual.empty()) {
        return ok(suite, name);
    }

    NYX_LOGE("%s %s: expected non-empty value", suite, name);
    return false;
}

} // namespace expect
} // namespace test
} // namespace sdk
} // namespace nyx
