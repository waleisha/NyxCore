#include "sdk/include/test.h"

#include "src/utils/string/format.h"
#include "src/utils/string/hex.h"
#include "src/utils/string/utf8.h"
#include "src/tests/support/test_assert.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace nyx {
namespace sdk {
namespace test {

namespace {

constexpr const char* kSuite = "string doctor";

bool expect_bool(const char* name, bool value, bool expected) {
    return expect::bool_eq(kSuite, name, value, expected);
}

bool expect_size(const char* name, std::size_t value, std::size_t expected) {
    return expect::size_eq(kSuite, name, value, expected);
}

bool expect_text(const char* name, const std::string& value, const std::string& expected) {
    return expect::text_eq(kSuite, name, value, expected);
}

bool expect_contains(const char* name, const std::string& value, std::string_view expected) {
    return expect::contains(kSuite, name, value, expected);
}

std::string bytes(std::initializer_list<unsigned int> values) {
    std::string out;
    out.reserve(values.size());
    for (const unsigned int value : values) {
        out.push_back(static_cast<char>(value));
    }
    return out;
}

bool check_utf8() {
    namespace text = ::nyx::utils::string;

    bool ok = true;
    const std::string valid = std::string("A") + bytes({0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd});
    const std::string overlong = bytes({0xc0, 0xaf});
    const std::string surrogate = bytes({0xed, 0xa0, 0x80});
    const std::string too_large = bytes({0xf4, 0x90, 0x80, 0x80});
    const std::string truncated = bytes({0xe2, 0x82});
    const std::string dirty = std::string("a") + overlong + "b";
    const std::string replacement = bytes({0xef, 0xbf, 0xbd});

    ok = expect_bool("valid utf8", text::is_utf8(valid), true) && ok;
    ok = expect_size("valid utf8 length", text::utf8_length(valid), 3) && ok;
    ok = expect_bool("overlong rejection", text::is_utf8(overlong), false) && ok;
    ok = expect_bool("surrogate rejection", text::is_utf8(surrogate), false) && ok;
    ok = expect_bool("range rejection", text::is_utf8(too_large), false) && ok;
    ok = expect_size("invalid utf8 length", text::utf8_length(truncated), std::string_view::npos) && ok;
    ok = expect_text("clean utf8", text::clean_utf8(dirty), "a" + replacement + replacement + "b") && ok;
    return ok;
}

bool check_hex() {
    namespace text = ::nyx::utils::string;

    bool ok = true;
    const std::uint8_t data[] = {0x00, 0x0f, 0x10, 0xff};
    std::vector<std::uint8_t> parsed;

    ok = expect_text("lower hex", text::hex(data, sizeof(data)), "000f10ff") && ok;
    ok = expect_text("upper hex", text::hex(data, sizeof(data), text::HexCase::Upper), "000F10FF") && ok;
    ok = expect_bool("parse hex", text::parse_hex("000F10ff", &parsed), true) && ok;
    ok = expect_size("parse hex size", parsed.size(), 4) && ok;
    ok = expect_bool(
        "parse hex bytes",
        parsed.size() == 4 && parsed[0] == 0x00 && parsed[1] == 0x0f && parsed[2] == 0x10 && parsed[3] == 0xff,
        true
    ) && ok;

    parsed = {0xaa};
    ok = expect_bool("parse odd rejection", text::parse_hex("0", &parsed), false) && ok;
    ok = expect_size("parse rejection keeps output", parsed.size(), 1) && ok;
    ok = expect_bool("parse invalid rejection", text::parse_hex("xx", &parsed), false) && ok;

    const std::uint8_t dump_data[] = {0x00, 0x41, 0x7f, 0x80, 0xff};
    const std::string dump = text::hex_dump(dump_data, sizeof(dump_data), 8);
    ok = expect_contains("hex dump offset", dump, "00000000") && ok;
    ok = expect_contains("hex dump bytes", dump, "00 41 7f 80 ff") && ok;
    ok = expect_contains("hex dump ascii", dump, "|.A...|") && ok;
    return ok;
}

bool check_format() {
    namespace text = ::nyx::utils::string;

    bool ok = true;
    char small[6] = {};
    char full[16] = {};

    ok = expect_text("format", text::format("%s-%d", "value", 7), "value-7") && ok;
    ok = expect_bool("format null", text::format(nullptr).empty(), true) && ok;
    ok = expect_bool("format_to full", text::format_to(full, sizeof(full), "%s-%d", "ok", 3), true) && ok;
    ok = expect_bool("format_to full text", std::strcmp(full, "ok-3") == 0, true) && ok;
    ok = expect_bool("format_to truncates", text::format_to(small, sizeof(small), "%s", "abcdef"), false) && ok;
    ok = expect_bool("format_to trunc text", std::strcmp(small, "abcde") == 0, true) && ok;
    ok = expect_bool("format_to null buffer", text::format_to(nullptr, 0, "%s", "x"), false) && ok;
    return ok;
}

} // namespace

bool CheckString() {
    bool ok = true;
    ok = check_utf8() && ok;
    ok = check_hex() && ok;
    ok = check_format() && ok;

    NYX_LOGI("string doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
