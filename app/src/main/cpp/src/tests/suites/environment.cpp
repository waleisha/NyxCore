#include "sdk/include/test.h"

#include "sdk/include/utils.h"
#include "src/tests/support/test_assert.h"

namespace nyx {
namespace sdk {
namespace test {

bool CheckEnv() {
    constexpr const char* kSuite = "env doctor";
    bool ok = true;

    NYX_LOGI(
        "env doctor started debug=%d uptime=%llu us",
        NYX_DEBUG_MODE,
        static_cast<unsigned long long>(utils::Uptime())
    );

    if (!utils::IsMain()) {
        NYX_LOGW("env doctor is not running on the runtime main thread");
    }

    bool task_ran = false;
    utils::Post([&task_ran]() {
        task_ran = true;
        NYX_LOGD("dispatcher task ran");
    });

    const auto drained = utils::RunTasks();
    ok = expect::true_value(kSuite, "dispatcher task ran", task_ran) && ok;
    ok = expect::true_value(kSuite, "dispatcher drained queue", drained > 0) && ok;

    NYX_LOGI("env doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
