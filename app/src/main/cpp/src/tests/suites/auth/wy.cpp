#include "src/core/auth/providers/wy/wy_codec.h"
#include "src/core/auth/providers/wy/wy_profile.h"
#include "src/core/auth/providers/wy/wy_provider.h"
#include "src/core/auth/providers/wy/wy_request.h"
#include "src/core/auth/providers/wy/wy_response.h"
#include "src/core/auth/providers/wy/wy_verify.h"

#include "nlohmann/json.hpp"
#include "sdk/include/utils.h"
#include "src/tests/support/test_assert.h"

#include <cstdint>
#include <string>
#include <utility>

namespace nyx {
namespace core {
namespace auth {
namespace doctor {

namespace {

using json = nlohmann::json;

constexpr const char* kSuite = "wy doctor";

bool expect_bool(const char* name, bool value, bool expected) {
    return ::nyx::sdk::test::expect::bool_eq(kSuite, name, value, expected);
}

bool expect_text(const char* name, const std::string& value, const std::string& expected) {
    return ::nyx::sdk::test::expect::text_eq(kSuite, name, value, expected);
}

bool expect_int(const char* name, int value, int expected) {
    if (value == expected) {
        NYX_LOGI("%s %s: passed", kSuite, name);
        return true;
    }

    NYX_LOGE("%s %s: expected %d, got %d", kSuite, name, expected, value);
    return false;
}

bool runtime_text_is_plain(const nyx::core::auth::wy::RuntimeText& text, const std::string& plain) {
    if (text.bytes.size() != plain.size()) {
        return false;
    }

    for (std::size_t i = 0; i < plain.size(); ++i) {
        if (text.bytes[i] != static_cast<std::uint8_t>(plain[i])) {
            return false;
        }
    }
    return true;
}

std::string check_login(
    const nyx::core::auth::wy::Profile& profile,
    const nyx::core::auth::wy::Request& request,
    std::int64_t server_time
) {
    const std::string stamp = std::to_string(server_time);
    return nyx::core::auth::wy::sha256_hex(
        nyx::core::auth::wy::md5_hex(
            stamp + profile.app_key + request.nonce + profile.check.salt
        )
    );
}

std::string check_heartbeat(
    const nyx::core::auth::wy::Profile& profile,
    const nyx::core::auth::wy::Request& request,
    std::int64_t server_time
) {
    return nyx::core::auth::wy::md5_hex(
        std::to_string(server_time) + profile.app_key + request.nonce
    );
}

std::string check_var(
    const nyx::core::auth::wy::Profile& profile,
    const nyx::core::auth::wy::Request& request,
    std::int64_t server_time
) {
    return nyx::core::auth::wy::md5_hex(
        std::to_string(server_time) + profile.app_key + request.nonce
    );
}

} // namespace

bool RunWyDoctor() {
    using namespace nyx::core::auth;
    using namespace nyx::core::auth::wy;

    bool ok = true;
    const Profile profile = sample_profile();
    const auto provider = MakeWyProvider(profile);
    ok = expect_bool("provider created", provider != nullptr, true) && ok;
    ok = expect_bool("empty provider", MakeWyProvider(Profile{}) == nullptr, true) && ok;
    Profile missing_heartbeat = profile;
    missing_heartbeat.calls.heartbeat.id.clear();
    ok = expect_bool("missing heartbeat call", MakeWyProvider(missing_heartbeat) == nullptr, true) && ok;
    Profile missing_var = profile;
    missing_var.calls.var.id.clear();
    ok = expect_bool("missing var call", MakeWyProvider(missing_var) == nullptr, true) && ok;
    Profile duplicate_alphabet = profile;
    duplicate_alphabet.codec.alphabet[1] = duplicate_alphabet.codec.alphabet[0];
    ok = expect_bool("duplicate alphabet", MakeWyProvider(duplicate_alphabet) == nullptr, true) && ok;
    ok = expect_text("var alias", resolve_var(profile, "payload_seed"), "remote_payload_seed") && ok;
    ok = expect_text("second var alias", resolve_var(profile, "feature_flag"), "remote_feature_flag") && ok;

    const RuntimeProfile runtime_profile = store_profile(profile);
    const Profile opened_profile = open_profile(runtime_profile);
    ok = expect_text("runtime profile host", opened_profile.endpoint.host, profile.endpoint.host) && ok;
    ok = expect_bool(
        "runtime profile host not plain",
        runtime_text_is_plain(runtime_profile.endpoint.host, profile.endpoint.host),
        false
    ) && ok;

    const LoginInput login_input{"doctor-license", "doctor-device"};
    const SessionInput session_input{"doctor-license", "doctor-device", "doctor-token"};
    const VarInput var_input{"doctor-license", "doctor-device", "payload_seed", "doctor-token"};

    const Request login_request = make_login_request(profile, login_input, 1700000123);
    const std::string login_body = "id=login&kami=doctor-license&markcode=doctor-device&t=1700000123&sign=" +
        md5_hex("kami=doctor-license&markcode=doctor-device&t=1700000123&" + profile.app_key) +
        "&value=000123";
    ok = expect_text("login path", login_request.path, endpoint_path(profile)) && ok;
    ok = expect_text("login body", login_request.body, login_body) && ok;

    const Request heartbeat_request = make_heartbeat_request(profile, session_input, 1700000456);
    const std::string heartbeat_body = "id=heartbeat&kami=doctor-license&markcode=doctor-device&t=1700000456&sign=" +
        md5_hex("kami=doctor-license&markcode=doctor-device&t=1700000456&kamitoken=doctor-token&" + profile.app_key) +
        "&kamitoken=doctor-token&value=000456";
    ok = expect_text("heartbeat body", heartbeat_request.body, heartbeat_body) && ok;

    const Request var_request = make_var_request(profile, var_input, 1700000789);
    const std::string var_body = "id=var&kami=doctor-license&markcode=doctor-device&t=1700000789&sign=" +
        md5_hex("kami=doctor-license&markcode=doctor-device&t=1700000789&key=payload_seed&" + profile.app_key) +
        "&value=000789&key=payload_seed";
    ok = expect_text("var body", var_request.body, var_body) && ok;

    const std::string plain = "{\"ok\":true,\"value\":\"seed\"}";
    const std::string encoded = encode(profile, plain);
    ok = expect_bool("codec encode", !encoded.empty(), true) && ok;
    ok = expect_text("codec roundtrip", decode(profile, encoded), plain) && ok;
    Profile base64_only = profile;
    base64_only.codec.response_steps = {CodecStep::DefBase};
    ok = expect_text("codec rejects invalid base64", decode(base64_only, "abc!"), "") && ok;

    json login_msg;
    login_msg["token"] = "doctor-token";
    login_msg["vip_expire"] = 1700003723;
    login_msg["remaining_count"] = 8;
    login_msg["features"] = json::array({
        json{{"feature", "camera_assist"}, {"allowed", true}, {"expires_at", 1700003723}},
        json{{"feature", "blocked_feature"}, {"allowed", false}, {"expires_at", 1700003723}}
    });
    login_msg["message"] = "login ok";

    json login_root;
    login_root["code"] = profile.calls.login.success_code;
    login_root["time"] = 1700000126;
    login_root["check"] = check_login(profile, login_request, 1700000126);
    login_root["msg"] = login_msg;

    ParsedResponse login_parsed;
    ok = expect_bool("login parse", parse(decode(profile, encode(profile, login_root.dump())), &login_parsed), true) && ok;
    const ProviderResult login_result = make_login_result(profile, login_request, login_parsed);
    ok = expect_bool("login result", login_result.success, true) && ok;
    ok = expect_text("login token", login_result.token, "doctor-token") && ok;
    ok = expect_text("login message", login_result.message, "login ok") && ok;
    ok = expect_bool("login expiry", login_result.expires_at == 1700003723, true) && ok;
    ok = expect_bool("login features", login_result.features.size() == 2, true) && ok;

    json large_count_root;
    large_count_root["code"] = profile.calls.login.success_code;
    large_count_root["time"] = 1700000126;
    large_count_root["check"] = check_login(profile, login_request, 1700000126);
    large_count_root["remaining_count"] = 999999999999LL;
    ParsedResponse large_count_parsed;
    ok = expect_bool("large count parse", parse(large_count_root.dump(), &large_count_parsed), true) && ok;
    ok = expect_int("large count ignored", large_count_parsed.remaining_uses, 0) && ok;

    json huge_count_root;
    huge_count_root["code"] = profile.calls.login.success_code;
    huge_count_root["time"] = 1700000126;
    huge_count_root["check"] = check_login(profile, login_request, 1700000126);
    huge_count_root["remaining_count"] = "999999999999999999999999999999";
    ParsedResponse huge_count_parsed;
    ok = expect_bool("huge count string parse", parse(huge_count_root.dump(), &huge_count_parsed), true) && ok;
    ok = expect_int("huge count string ignored", huge_count_parsed.remaining_uses, 0) && ok;
    ok = expect_text("huge count string not value", huge_count_parsed.value, "") && ok;

    json short_message_root = login_root;
    short_message_root["token"] = "doctor-token";
    short_message_root["message"] = "A1b2C";
    ParsedResponse short_message_parsed;
    ok = expect_bool("short message parse", parse(short_message_root.dump(), &short_message_parsed), true) && ok;
    ok = expect_text("short message keeps token", short_message_parsed.token, "doctor-token") && ok;
    ok = expect_text("short message not token", short_message_parsed.message, "A1b2C") && ok;

    json nested_override_root = login_root;
    nested_override_root["msg"] = json{
        {"code", 99999},
        {"time", 2000000000},
        {"token", "doctor-token"},
        {"message", "nested ok"}
    };
    ParsedResponse nested_override_parsed;
    ok = expect_bool("nested override parse", parse(nested_override_root.dump(), &nested_override_parsed), true) && ok;
    ok = expect_bool("nested code cannot override", nested_override_parsed.code == profile.calls.login.success_code, true) && ok;
    ok = expect_bool("nested time cannot override", nested_override_parsed.server_time == 1700000126, true) && ok;

    const std::string hash_message = "0123456789abcdef0123456789abcdef";
    json hash_message_root = login_root;
    hash_message_root["message"] = hash_message;
    ParsedResponse hash_message_parsed;
    ok = expect_bool("hash message parse", parse(hash_message_root.dump(), &hash_message_parsed), true) && ok;
    ok = expect_text("hash message preserved", hash_message_parsed.message, hash_message) && ok;
    ok = expect_text("hash message does not replace check", hash_message_parsed.check, check_login(profile, login_request, 1700000126)) && ok;

    json hash_only_message_root;
    hash_only_message_root["code"] = profile.calls.login.success_code;
    hash_only_message_root["time"] = 1700000126;
    hash_only_message_root["message"] = hash_message;
    ParsedResponse hash_only_message_parsed;
    ok = expect_bool("hash only message parse", parse(hash_only_message_root.dump(), &hash_only_message_parsed), true) && ok;
    ok = expect_bool("hash only message is not check", hash_only_message_parsed.has_check, false) && ok;

    json legacy_login_msg;
    legacy_login_msg["token"] = "doctor-token";
    legacy_login_msg["gf8a09e09dd0606e51a2f2e85375467bd"] = "code";
    legacy_login_msg["y5c05c0033524f747e47ff039ba415812"] = 1700003723;
    legacy_login_msg["check"] = check_login(profile, login_request, 1700000126);
    legacy_login_msg["code"] = 99999;
    legacy_login_msg["time"] = 2000000000;

    json legacy_login_root;
    legacy_login_root["xd66d97c2905526fa5f21e879ef42c4e2"] = profile.calls.login.success_code;
    legacy_login_root["q9f99cb210ef5adab20379f349b7b7131"] = legacy_login_msg;
    legacy_login_root["k4c0ce3282237ae75d37a4bb54516d240"] = 1700000126;

    ParsedResponse legacy_login_parsed;
    ok = expect_bool("legacy login parse", parse(legacy_login_root.dump(), &legacy_login_parsed), true) && ok;
    const ProviderResult legacy_login_result = make_login_result(profile, login_request, legacy_login_parsed);
    ok = expect_bool("legacy login result", legacy_login_result.success, true) && ok;
    ok = expect_text("legacy login token", legacy_login_result.token, "doctor-token") && ok;
    ok = expect_bool("legacy nested code cannot override", legacy_login_parsed.code == profile.calls.login.success_code, true) && ok;
    ok = expect_bool("legacy nested time cannot override", legacy_login_parsed.server_time == 1700000126, true) && ok;

    json heartbeat_root;
    heartbeat_root["code"] = profile.calls.heartbeat.success_code;
    heartbeat_root["time"] = 1700000457;
    heartbeat_root["check"] = check_heartbeat(profile, heartbeat_request, 1700000457);
    heartbeat_root["msg"] = "heartbeat ok";

    ParsedResponse heartbeat_parsed;
    ok = expect_bool("heartbeat parse", parse(decode(profile, encode(profile, heartbeat_root.dump())), &heartbeat_parsed), true) && ok;
    const ProviderResult heartbeat_result = make_heartbeat_result(profile, heartbeat_request, heartbeat_parsed);
    ok = expect_bool("heartbeat result", heartbeat_result.success, true) && ok;
    ok = expect_text("heartbeat message", heartbeat_result.message, "heartbeat ok") && ok;
    ok = expect_bool("heartbeat keeps expiry", heartbeat_result.expires_at == 0, true) && ok;

    json var_root;
    var_root["code"] = profile.calls.var.success_code;
    var_root["time"] = 1700000791;
    var_root["check"] = check_var(profile, var_request, 1700000791);
    var_root["msg"] = json{{"value", "seed-1"}};

    ParsedResponse var_parsed;
    ok = expect_bool("var parse", parse(decode(profile, encode(profile, var_root.dump())), &var_parsed), true) && ok;
    const ProviderResult var_result = make_var_result(profile, var_request, var_parsed);
    ok = expect_bool("var result", var_result.success, true) && ok;
    ok = expect_text("var value", var_result.value, "seed-1") && ok;
    ok = expect_bool("var keeps expiry", var_result.expires_at == 0, true) && ok;

    json legacy_var_msg;
    legacy_var_msg["check"] = check_var(profile, var_request, 1700000791);
    legacy_var_msg["c6b4fd575e5ab4ff6ef45ed262345bde"] = "seed-2";

    json legacy_var_root;
    legacy_var_root["xd66d97c2905526fa5f21e879ef42c4e2"] = profile.calls.var.success_code;
    legacy_var_root["q9f99cb210ef5adab20379f349b7b7131"] = legacy_var_msg;
    legacy_var_root["k4c0ce3282237ae75d37a4bb54516d240"] = 1700000791;

    ParsedResponse legacy_var_parsed;
    ok = expect_bool("legacy var parse", parse(legacy_var_root.dump(), &legacy_var_parsed), true) && ok;
    const ProviderResult legacy_var_result = make_var_result(profile, var_request, legacy_var_parsed);
    ok = expect_bool("legacy var result", legacy_var_result.success, true) && ok;
    ok = expect_text("legacy var value", legacy_var_result.value, "seed-2") && ok;

    login_parsed.check = "bad";
    const ProviderResult bad_login = make_login_result(profile, login_request, login_parsed);
    ok = expect_bool("bad login check", bad_login.success, false) && ok;
    ok = expect_bool("bad login failure", bad_login.failure == sdk::auth::Err::Protocol, true) && ok;

    NYX_LOGI("wy doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace doctor
} // namespace auth
} // namespace core
} // namespace nyx
