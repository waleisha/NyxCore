#include "src/engines/base/engine_status.h"
#include "src/engines/unity/unity_binding.h"
#include "src/runtime/runtime_result.h"
#include "sdk/include/hook.h"
#include "sdk/include/utils.h"

#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

#include <dlfcn.h>

namespace {

constexpr const char* kIl2CppLibrary = "libil2cpp.so";
constexpr const char* kImageName = "Assembly-CSharp.dll";
constexpr const char* kTestClass = "Test";
constexpr const char* kValueHolderClass = "ValueHolder";
constexpr const char* kBoolMethod = "getBool";
constexpr const char* kBoolField = "v_bool";
constexpr const char* kBoolType = "System.Boolean";
constexpr const char* kFloatType = "System.Single";
constexpr const char* kIntType = "System.Int32";
constexpr const char* kStringType = "System.String";
constexpr const char* kVoidType = "System.Void";
constexpr const char* kHookedTitle = "NyxCore Hooked Title";
constexpr int kAttempts = 120;
constexpr auto kDelay = std::chrono::milliseconds(500);
constexpr auto kIl2CppSettleDelay = std::chrono::seconds(2);
constexpr auto kHookWaitDelay = std::chrono::milliseconds(250);
constexpr int kHookWaitAttempts = 40;

struct MethodInfoHead {
    void* method_pointer = nullptr;
};

using GetStringFn = void* (*)(void*, const nyx::engines::unity::MethodInfo*);
using DomainGetFn = nyx::engines::unity::Il2CppApi::DomainGet;
using ThreadAttachFn = void* (*)(void*);
using StringNewFn = void* (*)(const char*);

std::atomic<bool> g_hook_installed{false};
std::atomic<int> g_replacement_calls{0};
void* g_hook_target = nullptr;
GetStringFn g_original_get_string = nullptr;
DomainGetFn g_domain_get = nullptr;
ThreadAttachFn g_thread_attach = nullptr;
StringNewFn g_string_new = nullptr;

bool il2cpp_loaded() {
    dlerror();
    void* handle = dlopen(kIl2CppLibrary, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        return false;
    }

    dlclose(handle);
    return true;
}

nyx::engines::unity::MethodSignature method_signature(
    const char* name,
    const char* return_type,
    std::initializer_list<const char*> params
) {
    nyx::engines::unity::MethodSignature signature;
    signature.image_name = kImageName;
    signature.class_name = kValueHolderClass;
    signature.name = name;
    signature.return_type = return_type;
    signature.params.assign(params.begin(), params.end());
    return signature;
}

nyx::engines::unity::FieldSignature field_signature(const char* name, const char* type_name) {
    nyx::engines::unity::FieldSignature signature;
    signature.image_name = kImageName;
    signature.class_name = kValueHolderClass;
    signature.name = name;
    signature.type_name = type_name;
    return signature;
}

bool log_result(const char* name, const nyx::runtime::RuntimeResult& result) {
    if (result.ok()) {
        NYX_LOGI("unity probe %s: passed", name);
        return true;
    }

    NYX_LOGW(
        "unity probe %s: %s (%s)",
        name,
        nyx::runtime::status_name(result.status),
        result.detail.c_str()
    );
    return false;
}

bool log_sdk_result(const char* name, const nyx::sdk::Result& result) {
    if (result.ok()) {
        NYX_LOGI("unity probe %s: passed", name);
        return true;
    }

    NYX_LOGW(
        "unity probe %s: %s (%s)",
        name,
        nyx::sdk::StatusStr(result.status),
        result.detail.c_str()
    );
    return false;
}

bool expect_text(const char* name, const std::string& actual, const char* expected) {
    if (actual == expected) {
        NYX_LOGI("unity probe %s: passed", name);
        return true;
    }

    NYX_LOGW("unity probe %s: expected %s, got %s", name, expected, actual.c_str());
    return false;
}

bool expect_counter(const char* name, std::size_t before, std::size_t after) {
    if (after > before) {
        NYX_LOGI("unity probe %s: passed (%zu -> %zu)", name, before, after);
        return true;
    }

    NYX_LOGW("unity probe %s: cache counter did not increase (%zu -> %zu)", name, before, after);
    return false;
}

bool check_method(
    nyx::engines::unity::UnityBinding& binding,
    const char* name,
    const char* return_type,
    std::initializer_list<const char*> params
) {
    nyx::engines::unity::VerifiedMethod method;
    const auto result = binding.method(method_signature(name, return_type, params), &method);
    const std::string label = std::string("bind ValueHolder.") + name;
    if (!log_result(label.c_str(), result) || !method.valid()) {
        return false;
    }

    bool ok = expect_text((label + " return type").c_str(), method.return_type(), return_type);
    if (method.params().size() != params.size()) {
        NYX_LOGW("unity probe %s param count: expected %zu, got %zu",
            label.c_str(), params.size(), method.params().size());
        ok = false;
    } else {
        std::size_t index = 0;
        for (const char* expected : params) {
            const std::string param_label = label + " param " + std::to_string(index);
            ok = expect_text(param_label.c_str(), method.params()[index].type_name, expected) && ok;
            ++index;
        }
    }

    return ok;
}

bool check_field(
    nyx::engines::unity::UnityBinding& binding,
    const char* name,
    const char* type_name
) {
    nyx::engines::unity::VerifiedField field;
    const auto result = binding.field(field_signature(name, type_name), &field);
    const std::string label = std::string("bind ValueHolder.") + name;
    if (!log_result(label.c_str(), result) || !field.valid()) {
        return false;
    }

    return expect_text((label + " type").c_str(), field.type_name(), type_name);
}

bool check_denied(nyx::engines::unity::UnityBinding& binding) {
    nyx::engines::unity::VerifiedMethod method;
    const auto method_result = binding.method(method_signature(kBoolMethod, kIntType, {}), &method);
    bool ok = method_result.status == nyx::runtime::RuntimeStatus::Denied && !method.valid();
    if (ok) {
        NYX_LOGI("unity probe denies mismatched method signature: passed");
    } else {
        NYX_LOGW("unity probe denies mismatched method signature: failed (%s)",
            nyx::runtime::status_name(method_result.status));
    }

    nyx::engines::unity::VerifiedField field;
    const auto field_result = binding.field(field_signature(kBoolField, kIntType), &field);
    const bool field_ok = field_result.status == nyx::runtime::RuntimeStatus::Denied && !field.valid();
    if (field_ok) {
        NYX_LOGI("unity probe denies mismatched field signature: passed");
    } else {
        NYX_LOGW("unity probe denies mismatched field signature: failed (%s)",
            nyx::runtime::status_name(field_result.status));
    }

    return ok && field_ok;
}

void* il2cpp_symbol(const char* name) {
    dlerror();
    void* handle = dlopen(kIl2CppLibrary, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        return nullptr;
    }

    dlerror();
    void* symbol = dlsym(handle, name);
    const char* error = dlerror();
    dlclose(handle);
    return error == nullptr ? symbol : nullptr;
}

bool attach_il2cpp_thread() {
    if (g_domain_get == nullptr || g_thread_attach == nullptr) {
        return false;
    }

    void* domain = reinterpret_cast<void*>(g_domain_get());
    if (domain == nullptr) {
        return false;
    }

    return g_thread_attach(domain) != nullptr;
}

void* hooked_get_string(void* self, const nyx::engines::unity::MethodInfo* method) {
    const int calls = g_replacement_calls.fetch_add(1, std::memory_order_relaxed);
    if (calls == 0) {
        const bool attached = attach_il2cpp_thread();
        NYX_LOGI("unity hook sample replacement title: %s", kHookedTitle);
        if (attached) {
            NYX_LOGI("unity hook sample replacement thread attach: passed");
        } else {
            NYX_LOGW("unity hook sample replacement thread attach: failed");
        }
    } else {
        static_cast<void>(attach_il2cpp_thread());
    }

    if (g_string_new != nullptr) {
        void* title = g_string_new(kHookedTitle);
        if (title != nullptr) {
            return title;
        }
    }

    return g_original_get_string != nullptr ? g_original_get_string(self, method) : nullptr;
}

bool resolve_hook_api(const nyx::engines::unity::Il2CppApi& api) {
    g_domain_get = api.domain_get;
    g_thread_attach = reinterpret_cast<ThreadAttachFn>(il2cpp_symbol("il2cpp_thread_attach"));
    g_string_new = reinterpret_cast<StringNewFn>(il2cpp_symbol("il2cpp_string_new"));

    bool ok = true;
    ok = log_result(
        "hook sample il2cpp_thread_attach export",
        g_thread_attach != nullptr ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "missing il2cpp_thread_attach"}
    ) && ok;
    ok = log_result(
        "hook sample il2cpp_string_new export",
        g_string_new != nullptr ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "missing il2cpp_string_new"}
    ) && ok;

    const bool attached = attach_il2cpp_thread();
    ok = log_result(
        "hook sample il2cpp thread attach",
        attached ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::Failed, "il2cpp_thread_attach returned null"}
    ) && ok;

    void* sample = ok && g_string_new != nullptr ? g_string_new(kHookedTitle) : nullptr;
    ok = log_result(
        "hook sample il2cpp_string_new",
        sample != nullptr ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::Failed, "il2cpp_string_new returned null"}
    ) && ok;
    return ok;
}

bool wait_hook_sample() {
    for (int attempt = 0; attempt < kHookWaitAttempts; ++attempt) {
        if (g_replacement_calls.load(std::memory_order_relaxed) > 0) {
            NYX_LOGI("unity probe hook sample replacement called: passed");
            return true;
        }
        std::this_thread::sleep_for(kHookWaitDelay);
    }

    NYX_LOGW("unity probe hook sample replacement called: failed");
    return false;
}

bool check_hook_sample(nyx::engines::unity::UnityBinding& binding) {
    if (g_hook_installed.load(std::memory_order_acquire)) {
        return wait_hook_sample();
    }

    nyx::engines::unity::VerifiedMethod method;
    const auto bind = binding.method(method_signature("getString", kStringType, {}), &method);
    if (!log_result("bind hook sample ValueHolder.getString", bind) || !method.valid()) {
        return false;
    }

    const auto* head = reinterpret_cast<const MethodInfoHead*>(method.handle());
    void* method_pointer = head != nullptr ? head->method_pointer : nullptr;
    if (!log_result(
        "hook sample method pointer",
        method_pointer != nullptr ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "ValueHolder.getString method pointer is null"}
    )) {
        return false;
    }

    nyx::engines::unity::Il2CppApi api;
    nyx::engines::unity::Il2CppRuntime runtime;
    if (!log_result("hook sample il2cpp symbols", runtime.symbols(&api))) {
        return false;
    }
    if (!resolve_hook_api(api)) {
        return false;
    }

    g_hook_target = method_pointer;
    void* original = nullptr;
    const auto installed = nyx::sdk::hook::InlineRaw(
        method_pointer,
        reinterpret_cast<void*>(hooked_get_string),
        &original
    );
    if (!log_sdk_result("hook sample install getString", installed) || original == nullptr) {
        return false;
    }

    g_original_get_string = reinterpret_cast<GetStringFn>(original);
    g_replacement_calls.store(0, std::memory_order_relaxed);
    g_hook_installed.store(true, std::memory_order_release);
    return wait_hook_sample();
}

bool check_cache(nyx::engines::unity::UnityBinding& binding) {
    const auto before = binding.cache_stats();

    nyx::engines::unity::VerifiedMethod cached_method;
    if (!binding.method(method_signature(kBoolMethod, kBoolType, {}), &cached_method).ok() || !cached_method.valid()) {
        NYX_LOGW("unity probe cache method repeat failed");
        return false;
    }

    nyx::engines::unity::VerifiedField cached_field;
    if (!binding.field(field_signature(kBoolField, kBoolType), &cached_field).ok() || !cached_field.valid()) {
        NYX_LOGW("unity probe cache field repeat failed");
        return false;
    }

    const auto after = binding.cache_stats();
    bool ok = expect_counter("image cache hit", before.image_hits, after.image_hits);
    ok = expect_counter("class cache hit", before.class_hits, after.class_hits) && ok;
    ok = expect_counter("method cache hit", before.method_hits, after.method_hits) && ok;
    ok = expect_counter("field cache hit", before.field_hits, after.field_hits) && ok;
    return ok;
}

bool check_events(nyx::engines::unity::UnityBinding& binding) {
    std::vector<nyx::engines::unity::BindingEvent> events;
    const auto result = binding.events(&events);
    if (!log_result("binding events read", result)) {
        return false;
    }

    bool has_verified_method = false;
    bool has_verified_field = false;
    bool has_denied_method = false;
    bool has_denied_field = false;
    for (const auto& event : events) {
        const bool method = event.kind == nyx::engines::unity::BindingKind::Method;
        const bool field = event.kind == nyx::engines::unity::BindingKind::Field;
        const bool ok = event.status == nyx::runtime::RuntimeStatus::Ok;
        const bool denied = event.status == nyx::runtime::RuntimeStatus::Denied;
        has_verified_method = has_verified_method || (method && ok && event.member_name == kBoolMethod);
        has_verified_field = has_verified_field || (field && ok && event.member_name == kBoolField);
        has_denied_method = has_denied_method || (method && denied && event.member_name == kBoolMethod);
        has_denied_field = has_denied_field || (field && denied && event.member_name == kBoolField);
    }

    bool ok = true;
    ok = log_result(
        "binding events include verified method",
        has_verified_method ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "verified method event missing"}
    ) && ok;
    ok = log_result(
        "binding events include verified field",
        has_verified_field ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "verified field event missing"}
    ) && ok;
    ok = log_result(
        "binding events include denied method",
        has_denied_method ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "denied method event missing"}
    ) && ok;
    ok = log_result(
        "binding events include denied field",
        has_denied_field ? nyx::runtime::RuntimeResult{} :
            nyx::runtime::RuntimeResult{nyx::runtime::RuntimeStatus::NotFound, "denied field event missing"}
    ) && ok;
    return ok;
}

bool check_once() {
    nyx::engines::unity::UnityBinding binding;
    const auto probe = binding.probe();
    if (probe.status != nyx::engines::EngineStatus::Available) {
        NYX_LOGI("unity probe resolver waiting: %s", probe.detail.c_str());
        return false;
    }
    NYX_LOGI("unity probe resolver available: %s", probe.detail.c_str());
    std::this_thread::sleep_for(kIl2CppSettleDelay);

    nyx::engines::unity::VerifiedField holder;
    NYX_LOGI("unity probe bind Test.holder: begin");
    const auto holder_result = binding.field({
        kImageName,
        "",
        kTestClass,
        "holder",
        kValueHolderClass
    }, &holder);
    if (!log_result("bind Test.holder", holder_result) || !holder.valid()) {
        return false;
    }
    NYX_LOGI("unity probe bind Test.holder: done");

    bool ok = true;
    ok = check_method(binding, "getBool", kBoolType, {}) && ok;
    ok = check_method(binding, "reverseBool", kVoidType, {}) && ok;
    ok = check_method(binding, "getInt", kIntType, {}) && ok;
    ok = check_method(binding, "addToInt", kVoidType, {kIntType}) && ok;
    ok = check_method(binding, "getFloat", kFloatType, {}) && ok;
    ok = check_method(binding, "getString", kStringType, {}) && ok;
    ok = check_method(binding, "setString", kVoidType, {kStringType}) && ok;
    ok = check_field(binding, "v_bool", kBoolType) && ok;
    ok = check_field(binding, "v_int", kIntType) && ok;
    ok = check_field(binding, "v_float", kFloatType) && ok;
    ok = check_field(binding, "v_string", kStringType) && ok;
    ok = check_denied(binding) && ok;
    ok = check_cache(binding) && ok;
    ok = check_events(binding) && ok;
    ok = check_hook_sample(binding) && ok;
    if (!ok) {
        return false;
    }

    NYX_LOGI("unity probe finished: passed");
    return true;
}

void run_probe() {
    NYX_LOGI("unity probe worker started");
    for (int attempt = 1; attempt <= kAttempts; ++attempt) {
        if (!il2cpp_loaded()) {
            if (attempt == 1 || attempt % 10 == 0) {
                NYX_LOGI("unity probe waiting for libil2cpp.so (%d/%d)", attempt, kAttempts);
            }
            std::this_thread::sleep_for(kDelay);
            continue;
        }

        if (check_once()) {
            return;
        }

        std::this_thread::sleep_for(kDelay);
    }

    NYX_LOGE("unity probe finished: failed");
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    NYX_LOGI("unity probe JNI_OnLoad");
    std::thread(run_probe).detach();
    return JNI_VERSION_1_6;
}
