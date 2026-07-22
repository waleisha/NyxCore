#include "sdk/include/test.h"

#include "src/core/manager/module_controller.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace nyx {
namespace sdk {
namespace test {

namespace {

struct Probe {
    int created = 0;
    int pre_inited = 0;
    int initialized = 0;
    int updated = 0;
    int rendered = 0;
};

Probe active_probe;
Probe disabled_probe;
Probe blocked_probe;
Probe allowed_probe;
Probe null_probe;
Probe failed_probe;
Probe active_failed_probe;
Probe sdk_info_probe;
std::string order_probe;

void reset_probes() {
    active_probe = Probe{};
    disabled_probe = Probe{};
    blocked_probe = Probe{};
    allowed_probe = Probe{};
    null_probe = Probe{};
    failed_probe = Probe{};
    active_failed_probe = Probe{};
    sdk_info_probe = Probe{};
    order_probe.clear();
}

class ProbeMod final : public IMod {
public:
    explicit ProbeMod(Probe& probe) : probe_(probe) {}

    void OnInit() override {
        ++probe_.pre_inited;
        ++probe_.initialized;
    }

    void OnUpdate() override {
        ++probe_.updated;
    }

    void OnDraw() override {
        ++probe_.rendered;
    }

private:
    Probe& probe_;
};

class FailingInitMod final : public IMod {
public:
    void OnInit() override {
        ++failed_probe.pre_inited;
        ++failed_probe.initialized;
        throw std::runtime_error("init failure");
    }

    void OnUpdate() override {
        ++failed_probe.updated;
    }

    void OnDraw() override {
        ++failed_probe.rendered;
    }
};

class FailingUpdateMod final : public IMod {
public:
    void OnInit() override {
        ++active_failed_probe.pre_inited;
        ++active_failed_probe.initialized;
    }

    void OnUpdate() override {
        ++active_failed_probe.updated;
        throw std::runtime_error("update failure");
    }

    void OnDraw() override {
        ++active_failed_probe.rendered;
    }
};

class OrderedMod final : public IMod {
public:
    explicit OrderedMod(char marker) : marker_(marker) {}

    void OnInit() override {}

    void OnUpdate() override {
        order_probe.push_back(marker_);
    }

    void OnDraw() override {
        order_probe.push_back(static_cast<char>(marker_ + ('a' - 'A')));
    }

private:
    char marker_;
};

std::unique_ptr<IMod> make_active() {
    ++active_probe.created;
    return std::make_unique<ProbeMod>(active_probe);
}

std::unique_ptr<IMod> make_disabled() {
    ++disabled_probe.created;
    return std::make_unique<ProbeMod>(disabled_probe);
}

std::unique_ptr<IMod> make_blocked() {
    ++blocked_probe.created;
    return std::make_unique<ProbeMod>(blocked_probe);
}

std::unique_ptr<IMod> make_allowed() {
    ++allowed_probe.created;
    return std::make_unique<ProbeMod>(allowed_probe);
}

std::unique_ptr<IMod> make_null() {
    ++null_probe.created;
    return nullptr;
}

std::unique_ptr<IMod> make_failed() {
    ++failed_probe.created;
    return std::make_unique<FailingInitMod>();
}

std::unique_ptr<IMod> make_active_failed() {
    ++active_failed_probe.created;
    return std::make_unique<FailingUpdateMod>();
}

std::unique_ptr<IMod> make_sdk_info() {
    ++sdk_info_probe.created;
    return std::make_unique<ProbeMod>(sdk_info_probe);
}

std::unique_ptr<IMod> make_order_a() {
    return std::make_unique<OrderedMod>('A');
}

std::unique_ptr<IMod> make_order_b() {
    return std::make_unique<OrderedMod>('B');
}

std::unique_ptr<IMod> make_order_c() {
    return std::make_unique<OrderedMod>('C');
}

bool auth_check(const char* feature) {
    return feature != nullptr && std::string(feature) == "allowed_feature";
}

bool expect_bool(const char* name, bool actual, bool expected) {
    if (actual == expected) {
        NYX_LOGI("module doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "module doctor %s: expected %s, got %s",
        name,
        expected ? "true" : "false",
        actual ? "true" : "false"
    );
    return false;
}

bool expect_int(const char* name, int actual, int expected) {
    if (actual == expected) {
        NYX_LOGI("module doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("module doctor %s: expected %d, got %d", name, expected, actual);
    return false;
}

bool expect_state(const char* name, core::ModuleState actual, core::ModuleState expected) {
    if (actual == expected) {
        NYX_LOGI("module doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "module doctor %s: expected state %d, got %d",
        name,
        static_cast<int>(expected),
        static_cast<int>(actual)
    );
    return false;
}

bool expect_non_empty(const char* name, const std::string& value) {
    if (!value.empty()) {
        NYX_LOGI("module doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("module doctor %s: expected non-empty value", name);
    return false;
}

bool expect_string(const char* name, const std::string& actual, const char* expected) {
    if (actual == expected) {
        NYX_LOGI("module doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("module doctor %s: expected %s, got %s", name, expected, actual.c_str());
    return false;
}

bool check_active_module() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register active",
        controller.Register(core::ModuleSpec{"active", "", make_active, true}),
        true
    ) && ok;
    ok = expect_bool(
        "duplicate active registration",
        controller.Register(core::ModuleSpec{"active", "", make_active, true}),
        true
    ) && ok;
    ok = expect_int("active count", static_cast<int>(controller.Count()), 1) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();

    const auto snapshot = controller.Snapshot("active");
    ok = expect_state("active state", snapshot.state, core::ModuleState::Active) && ok;
    ok = expect_bool("active enabled", snapshot.enabled, true) && ok;
    ok = expect_bool("active authorized", snapshot.authorized, true) && ok;
    ok = expect_int("active created once", active_probe.created, 1) && ok;
    ok = expect_int("active pre-init once", active_probe.pre_inited, 1) && ok;
    ok = expect_int("active initialize once", active_probe.initialized, 1) && ok;
    ok = expect_int("active update once", active_probe.updated, 1) && ok;
    ok = expect_int("active render once", active_probe.rendered, 1) && ok;
    ok = expect_int("active update count", static_cast<int>(snapshot.update_count), 1) && ok;
    ok = expect_int("active render count", static_cast<int>(snapshot.render_count), 1) && ok;
    ok = expect_int("snapshot count", static_cast<int>(controller.Snapshots().size()), 1) && ok;
    ok = expect_state("missing snapshot state", controller.Snapshot("missing").state, core::ModuleState::NotFound) && ok;
    ok = expect_bool("missing snapshot found", controller.Snapshot("missing").found, false) && ok;
    return ok;
}

bool check_registration_metadata() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "duplicate conflicting registration",
        controller.Register(core::ModuleSpec{"conflict", "feature_a", make_active, true}),
        true
    ) && ok;
    ok = expect_bool(
        "reject conflicting registration",
        controller.Register(core::ModuleSpec{"conflict", "feature_b", make_active, true}),
        false
    ) && ok;
    ok = expect_int("conflict count", static_cast<int>(controller.Count()), 1) && ok;

    return ok;
}

bool check_multi_module_order() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool("register order a", controller.Register({"order_a", "", make_order_a, true}), true) && ok;
    ok = expect_bool("register order b", controller.Register({"order_b", "", make_order_b, true}), true) && ok;
    ok = expect_bool("register order c", controller.Register({"order_c", "", make_order_c, true}), true) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();

    ok = expect_string("multi-module frame order", order_probe, "ABCabc") && ok;
    return ok;
}

bool check_disabled_module() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register disabled",
        controller.Register(core::ModuleSpec{"disabled", "", make_disabled, false}),
        true
    ) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();

    auto snapshot = controller.Snapshot("disabled");
    ok = expect_state("disabled initial state", snapshot.state, core::ModuleState::Disabled) && ok;
    ok = expect_bool("disabled initial enabled", snapshot.enabled, false) && ok;
    ok = expect_int("disabled not created", disabled_probe.created, 0) && ok;

    ok = expect_bool("enable disabled", controller.SetEnabled("disabled", true), true) && ok;
    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();
    snapshot = controller.Snapshot("disabled");
    ok = expect_state("enabled disabled state", snapshot.state, core::ModuleState::Active) && ok;
    ok = expect_int("enabled disabled create once", disabled_probe.created, 1) && ok;
    ok = expect_int("enabled disabled init once", disabled_probe.initialized, 1) && ok;
    ok = expect_int("enabled disabled update", disabled_probe.updated, 1) && ok;
    ok = expect_int("enabled disabled render", disabled_probe.rendered, 1) && ok;

    ok = expect_bool("disable active", controller.SetEnabled("disabled", false), true) && ok;
    controller.UpdateAll();
    controller.RenderAll();
    snapshot = controller.Snapshot("disabled");
    ok = expect_state("disabled active state", snapshot.state, core::ModuleState::Disabled) && ok;
    ok = expect_int("disabled active no update", disabled_probe.updated, 1) && ok;
    ok = expect_int("disabled active no render", disabled_probe.rendered, 1) && ok;

    ok = expect_bool("re-enable active", controller.SetEnabled("disabled", true), true) && ok;
    controller.UpdateAll();
    snapshot = controller.Snapshot("disabled");
    ok = expect_state("re-enabled active state", snapshot.state, core::ModuleState::Active) && ok;
    ok = expect_int("re-enabled no reinit", disabled_probe.initialized, 1) && ok;
    ok = expect_int("re-enabled update", disabled_probe.updated, 2) && ok;
    return ok;
}

bool check_auth_modules() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register blocked",
        controller.Register(core::ModuleSpec{"blocked", "blocked_feature", make_blocked, true}),
        true
    ) && ok;
    ok = expect_bool(
        "register allowed",
        controller.Register(core::ModuleSpec{"allowed", "allowed_feature", make_allowed, true}),
        true
    ) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();

    auto blocked = controller.Snapshot("blocked");
    auto allowed = controller.Snapshot("allowed");
    ok = expect_state("blocked state", blocked.state, core::ModuleState::BlockedByAuth) && ok;
    ok = expect_bool("blocked authorized", blocked.authorized, false) && ok;
    ok = expect_non_empty("blocked error", blocked.last_error) && ok;
    ok = expect_int("blocked not created", blocked_probe.created, 0) && ok;
    ok = expect_int("blocked no preinit", blocked_probe.pre_inited, 0) && ok;
    ok = expect_int("blocked no init", blocked_probe.initialized, 0) && ok;
    ok = expect_int("blocked no update", blocked_probe.updated, 0) && ok;
    ok = expect_int("blocked no render", blocked_probe.rendered, 0) && ok;

    ok = expect_state("allowed state", allowed.state, core::ModuleState::Active) && ok;
    ok = expect_bool("allowed authorized", allowed.authorized, true) && ok;
    ok = expect_int("allowed init", allowed_probe.initialized, 1) && ok;
    ok = expect_int("allowed update", allowed_probe.updated, 1) && ok;
    ok = expect_int("allowed render", allowed_probe.rendered, 1) && ok;

    controller.RefreshAuth();
    blocked = controller.Snapshot("blocked");
    ok = expect_state("blocked refresh state", blocked.state, core::ModuleState::BlockedByAuth) && ok;
    ok = expect_int("blocked refresh no init", blocked_probe.initialized, 0) && ok;
    return ok;
}

bool check_sdk_register_info() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register sdk info",
        controller.Register(core::ModuleSpec{"sdk_info", "allowed_feature", make_sdk_info, false}),
        true
    ) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    auto snapshot = controller.Snapshot("sdk_info");
    ok = expect_state("sdk info disabled", snapshot.state, core::ModuleState::Disabled) && ok;
    ok = expect_int("sdk info not created", sdk_info_probe.created, 0) && ok;

    ok = expect_bool("enable sdk info", controller.SetEnabled("sdk_info", true), true) && ok;
    controller.PreInitAll();
    controller.InitializeAll();
    snapshot = controller.Snapshot("sdk_info");
    ok = expect_state("sdk info active", snapshot.state, core::ModuleState::Active) && ok;
    ok = expect_bool("sdk info authorized", snapshot.authorized, true) && ok;
    ok = expect_int("sdk info created once", sdk_info_probe.created, 1) && ok;
    return ok;
}

bool check_failed_modules() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register null factory result",
        controller.Register(core::ModuleSpec{"null", "", make_null, true}),
        true
    ) && ok;
    ok = expect_bool(
        "register failed init",
        controller.Register(core::ModuleSpec{"failed", "", make_failed, true}),
        true
    ) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();

    const auto null_snapshot = controller.Snapshot("null");
    const auto failed_snapshot = controller.Snapshot("failed");
    ok = expect_state("null failed state", null_snapshot.state, core::ModuleState::Failed) && ok;
    ok = expect_non_empty("null failed error", null_snapshot.last_error) && ok;
    ok = expect_int("null created once", null_probe.created, 1) && ok;
    ok = expect_state("init failed state", failed_snapshot.state, core::ModuleState::Failed) && ok;
    ok = expect_non_empty("init failed error", failed_snapshot.last_error) && ok;
    ok = expect_int("init failed preinit", failed_probe.pre_inited, 1) && ok;
    ok = expect_int("init failed initialize", failed_probe.initialized, 1) && ok;
    ok = expect_int("init failed no update", failed_probe.updated, 0) && ok;
    ok = expect_int("init failed no render", failed_probe.rendered, 0) && ok;
    return ok;
}

bool check_active_failure_leaves_cache() {
    core::ModuleController controller(auth_check);
    bool ok = true;

    ok = expect_bool(
        "register active failure",
        controller.Register(core::ModuleSpec{"active_failed", "", make_active_failed, true}),
        true
    ) && ok;

    controller.PreInitAll();
    controller.InitializeAll();
    controller.UpdateAll();
    controller.RenderAll();
    controller.UpdateAll();

    const auto snapshot = controller.Snapshot("active_failed");
    ok = expect_state("active failure state", snapshot.state, core::ModuleState::Failed) && ok;
    ok = expect_non_empty("active failure error", snapshot.last_error) && ok;
    ok = expect_int("active failure created", active_failed_probe.created, 1) && ok;
    ok = expect_int("active failure initialized", active_failed_probe.initialized, 1) && ok;
    ok = expect_int("active failure update once", active_failed_probe.updated, 1) && ok;
    ok = expect_int("active failure no render", active_failed_probe.rendered, 0) && ok;
    ok = expect_int("active failure no update count", static_cast<int>(snapshot.update_count), 0) && ok;
    return ok;
}

} // namespace

bool CheckModule() {
    reset_probes();

    bool ok = true;
    ok = check_active_module() && ok;
    ok = check_registration_metadata() && ok;
    ok = check_multi_module_order() && ok;
    ok = check_disabled_module() && ok;
    ok = check_auth_modules() && ok;
    ok = check_sdk_register_info() && ok;
    ok = check_failed_modules() && ok;
    ok = check_active_failure_leaves_cache() && ok;

    NYX_LOGI("module doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
