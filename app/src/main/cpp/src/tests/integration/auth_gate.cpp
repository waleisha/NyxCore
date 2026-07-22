#include "sdk/include/test.h"

#include "sdk/include/auth.h"

#include <string>
#include <thread>

namespace nyx {
namespace sdk {
namespace test {

namespace {

auth::Result login_on_worker(const char* license) {
    auth::Result result;
    std::thread worker([&result, license]() {
        result = auth::Login(license);
    });
    worker.join();
    return result;
}

auth::Result fetch_on_worker(const char* key, std::string* value) {
    auth::Value<std::string> fetched;
    std::thread worker([&fetched, key]() {
        fetched = auth::TryGetVar(key);
    });
    worker.join();

    if (fetched.ok() && value != nullptr) {
        *value = fetched.value;
    }
    return fetched.result;
}

void log_result(const char* name, const auth::Result& result) {
    NYX_LOGI(
        "auth integration %s: success=%d failure=%d code=%d",
        name,
        result.success ? 1 : 0,
        static_cast<int>(result.failure),
        result.code
    );
}

} // namespace

bool CheckAuthIntegration(const char* license, const char* var_key) {
    if (license == nullptr || license[0] == '\0') {
        NYX_LOGI("auth integration gate skipped: no license configured");
        return true;
    }

    const auto login = login_on_worker(license);
    log_result("login", login);
    if (!login.success) {
        return false;
    }

    if (var_key != nullptr && var_key[0] != '\0') {
        std::string value;
        const auto var = fetch_on_worker(var_key, &value);
        log_result("fetch var", var);
        if (!var.success) {
            return false;
        }
    }

    NYX_LOGI("auth integration gate passed");
    return true;
}

} // namespace test
} // namespace sdk
} // namespace nyx
