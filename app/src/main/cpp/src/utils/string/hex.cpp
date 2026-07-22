#include "src/utils/string/hex.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace nyx {
namespace utils {
namespace string {

namespace {

const char* digits(HexCase letter_case) {
    return letter_case == HexCase::Upper ? "0123456789ABCDEF" : "0123456789abcdef";
}

void append_byte(std::string* out, std::uint8_t value, HexCase letter_case = HexCase::Lower) {
    const char* table = digits(letter_case);
    out->push_back(table[(value >> 4) & 0x0fU]);
    out->push_back(table[value & 0x0fU]);
}

int value_of(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool printable(std::uint8_t value) {
    return value >= 0x20U && value <= 0x7eU;
}

} // namespace

std::string hex(const void* data, std::size_t len, HexCase letter_case) {
    if (data == nullptr || len == 0) {
        return {};
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        append_byte(&out, bytes[i], letter_case);
    }
    return out;
}

std::string hex(std::string_view text, HexCase letter_case) {
    return hex(text.data(), text.size(), letter_case);
}

bool parse_hex(std::string_view text, std::vector<std::uint8_t>* out) {
    if (out == nullptr || (text.size() % 2) != 0) {
        return false;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = value_of(text[i]);
        const int low = value_of(text[i + 1]);
        if (high < 0 || low < 0) {
            return false;
        }

        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }

    *out = std::move(bytes);
    return true;
}

std::string hex_dump(const void* data, std::size_t len, std::size_t bytes_per_line) {
    if (data == nullptr || len == 0) {
        return {};
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::size_t width = bytes_per_line == 0 ? 16 : std::min<std::size_t>(bytes_per_line, 32);

    std::ostringstream out;
    for (std::size_t offset = 0; offset < len; offset += width) {
        const std::size_t line_len = std::min(width, len - offset);
        out << std::hex << std::setfill('0') << std::setw(8) << offset << "  ";

        for (std::size_t i = 0; i < width; ++i) {
            if (i < line_len) {
                out << std::setw(2) << static_cast<unsigned int>(bytes[offset + i]);
            } else {
                out << "  ";
            }
            if (i + 1 != width) {
                out << ' ';
            }
            if (i == 7 && width > 8) {
                out << ' ';
            }
        }

        out << "  |";
        for (std::size_t i = 0; i < line_len; ++i) {
            const std::uint8_t value = bytes[offset + i];
            out << (printable(value) ? static_cast<char>(value) : '.');
        }
        out << '|';

        if (offset + line_len < len) {
            out << '\n';
        }
    }

    return out.str();
}

std::string hex_dump(std::string_view text, std::size_t bytes_per_line) {
    return hex_dump(text.data(), text.size(), bytes_per_line);
}

} // namespace string
} // namespace utils
} // namespace nyx
