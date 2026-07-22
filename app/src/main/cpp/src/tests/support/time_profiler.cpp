#include "sdk/include/test.h"

#include <chrono>

namespace nyx {
namespace sdk {
namespace test {

std::uint64_t Now() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count()
    );
}

Timer::Timer(const char* name)
    : name_(name != nullptr ? name : "unnamed"),
      start_micros_(Now()) {}

Timer::~Timer() {
#if NYX_DEBUG_MODE
    const auto elapsed = Now() - start_micros_;
    if (elapsed >= NYX_BENCHMARK_LOG_MIN_US) {
        NYX_LOGD("%s took %llu us", name_, static_cast<unsigned long long>(elapsed));
    }
#endif
}

} // namespace test
} // namespace sdk
} // namespace nyx
