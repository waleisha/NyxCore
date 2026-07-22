#include "sdk/include/test.h"

namespace nyx {
namespace sdk {
namespace test {

namespace {

using GateCheck = bool (*)();

bool run_check(const char* name, GateCheck check) {
    if (check == nullptr) {
        NYX_LOGE("release gate %s: missing check", name);
        return false;
    }

    const auto passed = check();
    NYX_LOGI("release gate %s: %s", name, passed ? "passed" : "failed");
    return passed;
}

} // namespace

bool CheckRelease() {
    bool ok = true;
    ok = run_check("environment", CheckEnv) && ok;
    ok = run_check("string", CheckString) && ok;
    ok = run_check("crypto", CheckCrypto) && ok;
    ok = run_check("network", CheckNet) && ok;
    ok = run_check("auth", CheckAuth) && ok;
    ok = run_check("module", CheckModule) && ok;
    ok = run_check("runtime", CheckRuntime) && ok;
    ok = run_check("engine", CheckEngine) && ok;

    NYX_LOGI("release gate %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
