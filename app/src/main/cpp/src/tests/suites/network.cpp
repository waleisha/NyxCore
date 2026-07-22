#include "sdk/include/test.h"

#include "sdk/include/network.h"

#include "nlohmann/json.hpp"

#include <string>
#include <thread>
#include <utility>

namespace nyx {
namespace sdk {
namespace test {

namespace {

net::HttpResponse fetch_on_worker(
    std::string url,
    std::string endpoint,
    int conn_timeout,
    int read_timeout
) {
    net::HttpResponse response;
    std::thread worker([&response, url = std::move(url), endpoint = std::move(endpoint), conn_timeout, read_timeout]() {
        response = net::GetConfig(url, endpoint, conn_timeout, read_timeout);
    });
    worker.join();
    return response;
}

void log_check(const char* name, const net::HttpResponse& response, bool passed) {
    NYX_LOGI(
        "network doctor %s: name=%s status=%d error_code=%d",
        passed ? "passed" : "failed",
        name,
        response.status,
        response.error_code
    );
}

bool expect_error(const char* name, const net::HttpResponse& response, int expected) {
    const auto passed = response.error_code == expected;
    log_check(name, response, passed);

    if (passed) {
        return true;
    }

    NYX_LOGE(
        "network doctor expected error: name=%s got=%d expected=%d",
        name,
        response.error_code,
        expected
    );
    return false;
}

bool expect_client_path(const char* name, const net::HttpResponse& response) {
    const auto reached_client = response.status >= 0 || response.error_code != 0;
    const auto blocked_before_client =
        response.error_code == net::kInvalidUrl ||
        response.error_code == net::kInsecureUrl ||
        response.error_code == net::kMainThreadRequest ||
        response.error_code == net::kInvalidClient;
    const auto passed = reached_client && !blocked_before_client;
    log_check(name, response, passed);
    if (!passed) {
        NYX_LOGE(
            "network doctor expected client path: name=%s status=%d error_code=%d",
            name,
            response.status,
            response.error_code
        );
    }
    return passed;
}

bool has_ok_body(const std::string& body) {
    try {
        const auto parsed = nlohmann::json::parse(body);
        return parsed.value("ok", false);
    } catch (const nlohmann::json::exception& error) {
        NYX_LOGE("network integration json parse failed: %s", error.what());
        return false;
    }
}

} // namespace

bool CheckNet() {
    bool ok = true;

    const auto main_thread_response = net::GetConfig("https://example.invalid", "/", 1, 1);
    ok = expect_error("main-thread rejection", main_thread_response, net::kMainThreadRequest) && ok;

    const auto insecure_response = fetch_on_worker("http://example.invalid", "/", 1, 1);
    ok = expect_error("non-https rejection", insecure_response, net::kInsecureUrl) && ok;

    const auto empty_url_response = fetch_on_worker("", "/", 1, 1);
    ok = expect_error("empty-url rejection", empty_url_response, net::kInvalidUrl) && ok;

    const auto timeout_response = fetch_on_worker("https://127.0.0.1:1", "/", 0, -5);
    ok = expect_client_path("non-positive timeout fallback", timeout_response) && ok;

    NYX_LOGI("network doctor background https path skipped: no test url configured");
    NYX_LOGI("network doctor %s", ok ? "passed" : "failed");
    return ok;
}

#if NYX_ENABLE_INTEGRATION_GATES
bool CheckNetIntegration(const char* url) {
    if (url == nullptr || url[0] == '\0') {
        NYX_LOGI("network integration gate skipped: no test url configured");
        return true;
    }

    const auto response = fetch_on_worker(url, "/nyx/network-doctor.json", 3, 5);
    const auto passed =
        response.status == 200 &&
        response.error_code == 0 &&
        has_ok_body(response.body);
    log_check("https integration", response, passed);
    return passed;
}
#endif

} // namespace test
} // namespace sdk
} // namespace nyx
