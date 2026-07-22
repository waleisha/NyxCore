#include "sdk/include/test.h"

#include "sdk/include/auth.h"
#include "sdk/include/utils.h"
#include "src/tests/support/auth_state.h"
#include "src/tests/support/test_assert.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#ifndef NYX_AUTH_ENABLE_MOCK
#define NYX_AUTH_ENABLE_MOCK 0
#endif

namespace nyx {
namespace core {
namespace auth {

bool is_ready();

} // namespace auth
} // namespace core
} // namespace nyx

namespace nyx {
namespace sdk {
namespace test {

namespace {

using AuthCall = auth::Result (*)();

constexpr const char* kSuite = "auth doctor";
constexpr const char* kFilesDir = "/data/user/0/dev.nyxcore.manager/files/nyx-auth-doctor";

struct RestoreAuth {
    explicit RestoreAuth(core::auth::doctor::Snapshot snapshot)
        : snapshot_(std::move(snapshot)) {}

    ~RestoreAuth() {
        core::auth::doctor::Restore(snapshot_);
    }

    core::auth::doctor::Snapshot snapshot_;
};

auth::Result login_ok() {
    return auth::Login("doctor-license");
}

auth::Result login_empty() {
    return auth::Login("");
}

auth::Result fetch_seed() {
    return auth::TryGetVar("payload_seed").result;
}

auth::Result fetch_notice() {
    return auth::TryGetNotice().result;
}

auth::Result fetch_update() {
    return auth::TryCheckUpdate().result;
}

auth::InitConfig sdk_init_config() {
    auth::InitConfig config;
    auto& profile = config.profile;
    profile.host = "wy.example.invalid";
    profile.path_prefix = "/api/";
    profile.api_token = "wy-token";
    profile.app_key = "wy-app-key";

    profile.login_call_id = "login";
    profile.login_success_code = 78563;
    profile.heartbeat_call_id = "heartbeat";
    profile.heartbeat_success_code = 29646;
    profile.var_call_id = "var";
    profile.var_success_code = 33404;
    profile.notice_call_id = "notice";
    profile.notice_success_code = 37303;
    profile.update_call_id = "update";
    profile.update_success_code = 37405;

    profile.login_check_kind = 0;
    profile.heartbeat_check_kind = 2;
    profile.var_check_kind = 2;
    profile.salt = "wy-salt";
    profile.rc4_key = "wy-rc4-key";
    profile.alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    profile.license_field = "kami";
    profile.device_field = "markcode";
    profile.token_field = "kamitoken";
    profile.time_field = "t";
    profile.sign_field = "sign";
    profile.nonce_field = "value";
    profile.var_field = "key";
    profile.vars.emplace_back("payload_seed", "remote_payload_seed");
    return config;
}

auth::Result run_on_worker(AuthCall call) {
    auth::Result result;
    std::thread worker([&result, call]() {
        result = call();
    });
    worker.join();
    return result;
}

auth::Result run_on_render_thread(AuthCall call) {
    auth::Result result;
    std::thread worker([&result, call]() {
        core::auth::doctor::SetRenderThread(true);
        result = call();
        core::auth::doctor::SetRenderThread(false);
    });
    worker.join();
    return result;
}

auth::Value<std::string> fetch_seed_value_on_worker() {
    auth::Value<std::string> value;
    std::thread worker([&value]() {
        value = auth::TryGetVar("payload_seed");
    });
    worker.join();
    return value;
}

std::string get_seed_on_worker() {
    std::string value;
    std::thread worker([&value]() {
        value = auth::GetVar("payload_seed");
    });
    worker.join();
    return value;
}

void clear_files() {
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path(kFilesDir) / "nyx_auth", error);
}

auth::Result configure_context(
    const char* android_id = "doctor-android-id",
    const char* cert = "doctor-cert",
    const char* source_dir = "/data/app/dev.nyxcore.manager/base.apk",
    const auth::InitConfig* config = nullptr
) {
    auth::Context context;
    context.files_dir = kFilesDir;
    context.android_id = android_id;
    context.package_name = "dev.nyxcore.manager";
    context.source_dir = source_dir;
    context.cert_sha256 = cert;
    if (config != nullptr) {
        return auth::InitForTest(context, *config);
    }
    return auth::InitForTest(context, auth::InitConfig{});
}

void reset_mock_configured(const char* android_id = "doctor-android-id", const char* cert = "doctor-cert") {
    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context(android_id, cert);
}

bool tamper_file(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "tampered";
    return static_cast<bool>(out);
}

bool expect_result(
    const char* name,
    const auth::Result& result,
    bool success,
    auth::Err failure
) {
    const bool passed = result.success == success && result.failure == failure;
    if (passed) {
        NYX_LOGI("%s %s: passed", kSuite, name);
    } else {
        NYX_LOGE(
            "%s %s: expected success=%d failure=%d, got success=%d failure=%d code=%d",
            kSuite,
            name,
            success ? 1 : 0,
            static_cast<int>(failure),
            result.success ? 1 : 0,
            static_cast<int>(result.failure),
            result.code
        );
    }
    return passed;
}

bool expect_bool(const char* name, bool value, bool expected) {
    return expect::bool_eq(kSuite, name, value, expected);
}

bool expect_text(const char* name, const char* value, const char* expected) {
    return expect::text_eq(kSuite, name, value, expected);
}

bool expect_not_empty(const char* name, const std::string& value) {
    return expect::not_empty(kSuite, name, value);
}

bool expect_same_text(const char* name, const std::string& value, const std::string& expected) {
    return expect::text_eq(kSuite, name, value, expected);
}

bool expect_different_text(const char* name, const std::string& value, const std::string& expected) {
    const bool passed = !value.empty() && value != expected;
    if (passed) {
        NYX_LOGI("%s %s: passed", kSuite, name);
    } else {
        NYX_LOGE("%s %s: expected non-empty value different from %s", kSuite, name, expected.c_str());
    }
    return passed;
}

auth::Result heartbeat_on_worker() {
    auth::Result result;
    std::thread worker([&result]() {
        result = core::auth::doctor::Heartbeat();
    });
    worker.join();
    return result;
}

} // namespace

bool CheckAuth() {
    const RestoreAuth restore(core::auth::doctor::Save());
    clear_files();
    core::auth::doctor::Reset();

    bool ok = true;

    configure_context();
    core::auth::doctor::UseNoProvider();
    ok = expect_bool("wy readiness without config", nyx::core::auth::is_ready(), false) && ok;
    ok = expect_result(
        "login without wy config",
        run_on_worker(login_ok),
        false,
        auth::Err::LocalState
    ) && ok;

    core::auth::doctor::Reset();

    ok = core::auth::doctor::RunWyDoctor() && ok;

    auth::InitConfig invalid_wy_config;
    invalid_wy_config.profile.host = "wy.example.invalid";
    ok = expect_result(
        "sdk init rejects invalid wy profile",
        configure_context("doctor-android-id", "doctor-cert", "/data/app/dev.nyxcore.manager/base.apk", &invalid_wy_config),
        false,
        auth::Err::Rejected
    ) && ok;

    const auth::InitConfig wy_config = sdk_init_config();
    ok = expect_result(
        "sdk init accepts wy profile",
        configure_context("doctor-android-id", "doctor-cert", "/data/app/dev.nyxcore.manager/base.apk", &wy_config),
        true,
        auth::Err::None
    ) && ok;
    ok = expect_bool("sdk wy ready", nyx::core::auth::is_ready(), true) && ok;

    core::auth::doctor::Reset();

    ok = expect_result(
        "unconfigured login",
        run_on_worker(login_ok),
        false,
        auth::Err::LocalState
    ) && ok;

    configure_context();

    ok = expect_result(
        "empty license",
        run_on_worker(login_empty),
        false,
        auth::Err::Rejected
    ) && ok;

    ok = expect_result(
        "main-thread login rejection",
        auth::Login("doctor-license"),
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "main-thread fetch rejection",
        auth::TryGetVar("payload_seed").result,
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "main-thread notice rejection",
        auth::TryGetNotice().result,
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "main-thread update rejection",
        auth::TryCheckUpdate().result,
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "render-thread login rejection",
        run_on_render_thread(login_ok),
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "render-thread fetch rejection",
        run_on_render_thread(fetch_seed),
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "render-thread notice rejection",
        run_on_render_thread(fetch_notice),
        false,
        auth::Err::Runtime
    ) && ok;

    ok = expect_result(
        "render-thread update rejection",
        run_on_render_thread(fetch_update),
        false,
        auth::Err::Runtime
    ) && ok;

    core::auth::doctor::UseNoProvider();

    ok = expect_bool("session without provider", auth::IsLoggedIn(), false) && ok;
    ok = expect_bool("launch without session", auth::CanRun("camera_assist"), false) && ok;
    ok = expect_result(
        "fetch without provider",
        run_on_worker(fetch_seed),
        false,
        auth::Err::LocalState
    ) && ok;

    ok = expect_result(
        "notice without provider",
        run_on_worker(fetch_notice),
        false,
        auth::Err::LocalState
    ) && ok;

    ok = expect_result(
        "update without provider",
        run_on_worker(fetch_update),
        false,
        auth::Err::LocalState
    ) && ok;

#if NYX_AUTH_ENABLE_MOCK
    clear_files();
    reset_mock_configured();

    ok = expect_result(
        "login",
        run_on_worker(login_ok),
        true,
        auth::Err::None
    ) && ok;

    ok = expect_bool("session after login", auth::IsLoggedIn(), true) && ok;
    ok = expect_bool("session store after login", core::auth::doctor::IsLoggedInStore(), true) && ok;
    ok = expect_bool("allowed feature", auth::HasFeature("camera_assist"), true) && ok;
    ok = expect_bool("denied feature", auth::HasFeature("blocked_feature"), false) && ok;
    ok = expect_bool("expired feature", auth::HasFeature("expired_feature"), false) && ok;
    ok = expect_bool("launch gate", auth::CanRun("camera_assist"), true) && ok;

    auth::Capability payload_seed_capability;
    const auto exported_capability = auth::ExportCapability(auth::CapabilityPurpose::PayloadSeed);
    ok = expect_result("export payload capability", exported_capability.result, true, auth::Err::None) && ok;
    if (exported_capability.ok()) {
        payload_seed_capability = exported_capability.value;
    }
    ok = expect_bool(
        "verify payload capability",
        auth::VerifyCapability(auth::CapabilityPurpose::PayloadSeed, payload_seed_capability),
        true
    ) && ok;
    ok = expect_bool(
        "reject wrong capability purpose",
        auth::VerifyCapability(auth::CapabilityPurpose::UnityLoad, payload_seed_capability),
        false
    ) && ok;
    auth::Capability tampered_capability = payload_seed_capability;
    tampered_capability.word0 ^= 1u;
    ok = expect_bool(
        "reject tampered capability",
        auth::VerifyCapability(auth::CapabilityPurpose::PayloadSeed, tampered_capability),
        false
    ) && ok;

    const auto value_result = fetch_seed_value_on_worker();
    ok = expect_result("fetch value", value_result.result, true, auth::Err::None) && ok;
    ok = expect_same_text("fetch value text", value_result.value, "doctor-seed") && ok;
    ok = expect_same_text("direct fetch value text", get_seed_on_worker(), "doctor-seed") && ok;

    ok = expect_result(
        "mock notice unsupported",
        run_on_worker(fetch_notice),
        false,
        auth::Err::Protocol
    ) && ok;

    ok = expect_result(
        "mock update unsupported",
        run_on_worker(fetch_update),
        false,
        auth::Err::Protocol
    ) && ok;

    const std::string first_device_id = core::auth::doctor::DeviceId();
    ok = expect_not_empty("device id", first_device_id) && ok;

    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context();
    ok = expect_bool("restored session", auth::IsLoggedIn(), true) && ok;
    ok = expect_same_text("stable device id", core::auth::doctor::DeviceId(), first_device_id) && ok;

    clear_files();
    reset_mock_configured();
    ok = expect_bool("reinstall clears restored session", auth::IsLoggedIn(), false) && ok;

    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context("other-android-id");
    ok = expect_bool("backup restore clears session", auth::IsLoggedIn(), false) && ok;

    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context("doctor-android-id", "doctor-cert", "/data/app/dev.nyxcore.manager/base-v2.apk");
    ok = expect_bool("source dir change clears restored session", auth::IsLoggedIn(), false) && ok;

    clear_files();
    reset_mock_configured();
    ok = expect_result("login before tamper", run_on_worker(login_ok), true, auth::Err::None) && ok;
    ok = expect_bool("tamper wrote session", tamper_file(core::auth::doctor::SessionStorePath()), true) && ok;
    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context();
    ok = expect_bool("tampered session not restored", auth::IsLoggedIn(), false) && ok;

    clear_files();
    reset_mock_configured("");
    const std::string local_device_id = core::auth::doctor::DeviceId();
    ok = expect_not_empty("local device id", local_device_id) && ok;
    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context("");
    ok = expect_same_text("local device seal", core::auth::doctor::DeviceId(), local_device_id) && ok;
    ok = expect_bool("local login", run_on_worker(login_ok).success, true) && ok;
    ok = expect_bool("no android id skips session store", core::auth::doctor::IsLoggedInStore(), false) && ok;
    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context("");
    ok = expect_bool("no android id session not restored", auth::IsLoggedIn(), false) && ok;
    ok = expect_bool(
        "tamper wrote device",
        tamper_file(std::filesystem::path(kFilesDir) / "nyx_auth" / "device.seal"),
        true
    ) && ok;
    core::auth::doctor::Reset();
    core::auth::doctor::UseMock();
    configure_context("");
    ok = expect_different_text("tampered device seal rotates id", core::auth::doctor::DeviceId(), local_device_id) && ok;

    clear_files();
    reset_mock_configured();
    ok = expect_result("login before logout", run_on_worker(login_ok), true, auth::Err::None) && ok;
    ok = expect_bool("store before logout", core::auth::doctor::IsLoggedInStore(), true) && ok;
    auth::Logout();
    ok = expect_bool("session after logout", auth::IsLoggedIn(), false) && ok;
    ok = expect_bool("store after logout", core::auth::doctor::IsLoggedInStore(), false) && ok;
    ok = expect_bool(
        "capability after logout",
        auth::VerifyCapability(auth::CapabilityPurpose::PayloadSeed, payload_seed_capability),
        false
    ) && ok;
    ok = expect_result(
        "fetch without session",
        run_on_worker(fetch_seed),
        false,
        auth::Err::LocalState
    ) && ok;

    clear_files();
    reset_mock_configured();
    ok = expect_result("login for heartbeat", run_on_worker(login_ok), true, auth::Err::None) && ok;
    core::auth::doctor::SetHeartbeatFailures(1);
    ok = expect_result(
        "heartbeat soft failure",
        heartbeat_on_worker(),
        false,
        auth::Err::Network
    ) && ok;
    ok = expect_bool("session after soft failure", auth::IsLoggedIn(), true) && ok;

    core::auth::doctor::SetHeartbeatFailures(3);
    heartbeat_on_worker();
    heartbeat_on_worker();
    heartbeat_on_worker();
    ok = expect_bool("session after heartbeat limit", auth::IsLoggedIn(), false) && ok;
#endif

    clear_files();
    core::auth::doctor::Reset();
    NYX_LOGI("auth doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
