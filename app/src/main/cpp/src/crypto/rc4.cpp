#include "sdk/include/crypto.h"

#include <array>
#include <utility>

namespace nyx {
namespace sdk {
namespace crypt {

namespace {

class Rc4 {
public:
    static void crypt(const uint8_t* key, size_t key_len, uint8_t* data, size_t data_len) {
        if (key == nullptr || key_len == 0 || (data == nullptr && data_len > 0)) {
            NYX_LOGE("Rc4 received invalid buffer");
            return;
        }

        std::array<uint8_t, 256> state{};
        for (size_t i = 0; i < state.size(); ++i) {
            state[i] = static_cast<uint8_t>(i);
        }

        uint8_t j = 0;
        for (size_t i = 0; i < state.size(); ++i) {
            j = static_cast<uint8_t>(j + state[i] + key[i % key_len]);
            std::swap(state[i], state[j]);
        }

        uint8_t i = 0;
        j = 0;
        for (size_t n = 0; n < data_len; ++n) {
            i = static_cast<uint8_t>(i + 1);
            j = static_cast<uint8_t>(j + state[i]);
            std::swap(state[i], state[j]);
            const auto k = state[static_cast<uint8_t>(state[i] + state[j])];
            data[n] ^= k;
        }
    }
};

} // namespace

void Rc4(const uint8_t* key, size_t key_len, uint8_t* data, size_t data_len) {
    Rc4::crypt(key, key_len, data, data_len);
}

} // namespace crypt
} // namespace sdk
} // namespace nyx
