#include "sdk/include/test.h"

#include "sdk/include/engine.h"
#include "src/engines/unity/il2cpp_resolver.h"
#include "src/engines/unreal/ue_runtime.h"
#include "src/runtime/runtime_result.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace nyx {
namespace sdk {
namespace test {

namespace {

bool expect_status(const char* name, engines::EngineStatus actual, engines::EngineStatus expected) {
    if (actual == expected) {
        NYX_LOGI("engine doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "engine doctor %s: expected %s, got %s",
        name,
        engines::status_name(expected),
        engines::status_name(actual)
    );
    return false;
}

bool expect_runtime_status(const char* name, runtime::RuntimeStatus actual, runtime::RuntimeStatus expected) {
    if (actual == expected) {
        NYX_LOGI("engine doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "engine doctor %s: expected %s, got %s",
        name,
        runtime::status_name(expected),
        runtime::status_name(actual)
    );
    return false;
}

bool expect_sdk_status(const char* name, Status actual, Status expected) {
    if (actual == expected) {
        NYX_LOGI("engine doctor %s: passed", name);
        return true;
    }

    NYX_LOGE(
        "engine doctor %s: expected %s, got %s",
        name,
        StatusStr(expected),
        StatusStr(actual)
    );
    return false;
}

bool expect_not_failed(const char* name, const engines::EngineProbe& probe) {
    if (probe.status != engines::EngineStatus::Failed) {
        NYX_LOGI("engine doctor %s: %s", name, engines::status_name(probe.status));
        return true;
    }

    NYX_LOGE("engine doctor %s failed: %s", name, probe.detail.c_str());
    return false;
}

bool expect_true(const char* name, bool value) {
    if (value) {
        NYX_LOGI("engine doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("engine doctor %s: expected true", name);
    return false;
}

bool expect_empty(const char* name, const std::vector<engines::unity::Il2CppImageView>& images) {
    if (images.empty()) {
        NYX_LOGI("engine doctor %s: passed", name);
        return true;
    }

    NYX_LOGE("engine doctor %s: expected empty image list, got %zu", name, images.size());
    return false;
}

struct FakeType {
    const char* name = "";
};

struct FakeParam {
    const char* name = "";
    const FakeType* type = nullptr;
};

struct FakeMethod {
    const char* name = "";
    const FakeType* return_type = nullptr;
    const FakeParam* params = nullptr;
    std::uint32_t param_count = 0;
};

struct FakeClass {
    const char* name_space = "";
    const char* name = "";
    const FakeMethod* const* methods = nullptr;
    std::size_t method_count = 0;
};

struct FakeImage {
    const char* name = "";
    FakeClass* klass = nullptr;
};

struct FakeAssembly {
    FakeImage* image = nullptr;
};

struct FakeDomain {};

const FakeType kFakeVoid{"System.Void"};
const FakeType kFakeSingle{"System.Single"};
const FakeType kFakeInt{"System.Int32"};
const FakeType kFakeString{"System.String"};
const FakeParam kTickSingleParams[] = {FakeParam{"delta", &kFakeSingle}};
const FakeParam kTickIntParams[] = {FakeParam{"frame", &kFakeInt}};
const FakeMethod kTickSingle{"Tick", &kFakeVoid, kTickSingleParams, 1};
const FakeMethod kTickInt{"Tick", &kFakeInt, kTickIntParams, 1};
const FakeMethod* const kFakeMethods[] = {&kTickSingle, &kTickInt};
FakeClass kFakeClass{"Game", "Player", kFakeMethods, 2};
FakeImage kFakeImage{"Assembly-CSharp.dll", &kFakeClass};
FakeAssembly kFakeAssembly{&kFakeImage};
FakeDomain kFakeDomain;

engines::unity::Il2CppDomain* fake_domain_get() {
    return reinterpret_cast<engines::unity::Il2CppDomain*>(&kFakeDomain);
}

const engines::unity::Il2CppAssembly** fake_domain_get_assemblies(
    const engines::unity::Il2CppDomain*,
    std::size_t* count
) {
    static const engines::unity::Il2CppAssembly* assemblies[] = {
        reinterpret_cast<const engines::unity::Il2CppAssembly*>(&kFakeAssembly)
    };
    if (count != nullptr) {
        *count = 1;
    }
    return assemblies;
}

const engines::unity::Il2CppImage* fake_assembly_get_image(const engines::unity::Il2CppAssembly* assembly) {
    const auto* fake = reinterpret_cast<const FakeAssembly*>(assembly);
    return fake != nullptr ? reinterpret_cast<const engines::unity::Il2CppImage*>(fake->image) : nullptr;
}

const char* fake_image_get_name(const engines::unity::Il2CppImage* image) {
    const auto* fake = reinterpret_cast<const FakeImage*>(image);
    return fake != nullptr ? fake->name : "";
}

engines::unity::Il2CppClass* fake_class_from_name(
    const engines::unity::Il2CppImage* image,
    const char* name_space,
    const char* name
) {
    const auto* fake = reinterpret_cast<const FakeImage*>(image);
    if (fake == nullptr || fake->klass == nullptr) {
        return nullptr;
    }
    if (std::strcmp(fake->klass->name_space, name_space != nullptr ? name_space : "") != 0 ||
        std::strcmp(fake->klass->name, name != nullptr ? name : "") != 0) {
        return nullptr;
    }
    return reinterpret_cast<engines::unity::Il2CppClass*>(fake->klass);
}

const char* fake_class_get_name(engines::unity::Il2CppClass* klass) {
    const auto* fake = reinterpret_cast<const FakeClass*>(klass);
    return fake != nullptr ? fake->name : "";
}

const char* fake_class_get_namespace(engines::unity::Il2CppClass* klass) {
    const auto* fake = reinterpret_cast<const FakeClass*>(klass);
    return fake != nullptr ? fake->name_space : "";
}

const engines::unity::MethodInfo* fake_class_get_method_from_name(
    engines::unity::Il2CppClass* klass,
    const char* name,
    int arg_count
) {
    const auto* fake = reinterpret_cast<const FakeClass*>(klass);
    if (fake == nullptr) {
        return nullptr;
    }

    for (std::size_t i = 0; i < fake->method_count; ++i) {
        const FakeMethod* method = fake->methods[i];
        if (method != nullptr &&
            std::strcmp(method->name, name != nullptr ? name : "") == 0 &&
            (arg_count < 0 || method->param_count == static_cast<std::uint32_t>(arg_count))) {
            return reinterpret_cast<const engines::unity::MethodInfo*>(method);
        }
    }
    return nullptr;
}

const engines::unity::MethodInfo* fake_class_get_methods(
    engines::unity::Il2CppClass* klass,
    void** iter
) {
    const auto* fake = reinterpret_cast<const FakeClass*>(klass);
    if (fake == nullptr || iter == nullptr) {
        return nullptr;
    }

    const auto index = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(*iter));
    if (index >= fake->method_count) {
        return nullptr;
    }

    *iter = reinterpret_cast<void*>(index + 1);
    return reinterpret_cast<const engines::unity::MethodInfo*>(fake->methods[index]);
}

engines::unity::FieldInfo* fake_class_get_field_from_name(engines::unity::Il2CppClass*, const char*) {
    return nullptr;
}

const char* fake_method_get_name(const engines::unity::MethodInfo* method) {
    const auto* fake = reinterpret_cast<const FakeMethod*>(method);
    return fake != nullptr ? fake->name : "";
}

const engines::unity::Il2CppType* fake_method_get_return_type(const engines::unity::MethodInfo* method) {
    const auto* fake = reinterpret_cast<const FakeMethod*>(method);
    return fake != nullptr ? reinterpret_cast<const engines::unity::Il2CppType*>(fake->return_type) : nullptr;
}

std::uint32_t fake_method_get_param_count(const engines::unity::MethodInfo* method) {
    const auto* fake = reinterpret_cast<const FakeMethod*>(method);
    return fake != nullptr ? fake->param_count : 0;
}

const engines::unity::Il2CppType* fake_method_get_param(
    const engines::unity::MethodInfo* method,
    std::uint32_t index
) {
    const auto* fake = reinterpret_cast<const FakeMethod*>(method);
    if (fake == nullptr || index >= fake->param_count) {
        return nullptr;
    }
    return reinterpret_cast<const engines::unity::Il2CppType*>(fake->params[index].type);
}

const char* fake_method_get_param_name(const engines::unity::MethodInfo* method, std::uint32_t index) {
    const auto* fake = reinterpret_cast<const FakeMethod*>(method);
    if (fake == nullptr || index >= fake->param_count) {
        return "";
    }
    return fake->params[index].name;
}

const char* fake_field_get_name(engines::unity::FieldInfo*) {
    return "";
}

const engines::unity::Il2CppType* fake_field_get_type(engines::unity::FieldInfo*) {
    return reinterpret_cast<const engines::unity::Il2CppType*>(&kFakeVoid);
}

char* fake_type_get_name(const engines::unity::Il2CppType* type) {
    const auto* fake = reinterpret_cast<const FakeType*>(type);
    const char* name = fake != nullptr ? fake->name : "";
    const std::size_t size = std::strlen(name) + 1;
    auto* out = static_cast<char*>(std::malloc(size));
    if (out != nullptr) {
        std::memcpy(out, name, size);
    }
    return out;
}

void fake_free(void* pointer) {
    std::free(pointer);
}

engines::unity::Il2CppApi fake_api() {
    engines::unity::Il2CppApi api;
    api.domain_get = fake_domain_get;
    api.domain_get_assemblies = fake_domain_get_assemblies;
    api.assembly_get_image = fake_assembly_get_image;
    api.image_get_name = fake_image_get_name;
    api.class_from_name = fake_class_from_name;
    api.class_get_name = fake_class_get_name;
    api.class_get_namespace = fake_class_get_namespace;
    api.class_get_method_from_name = fake_class_get_method_from_name;
    api.class_get_methods = fake_class_get_methods;
    api.class_get_field_from_name = fake_class_get_field_from_name;
    api.method_get_name = fake_method_get_name;
    api.method_get_return_type = fake_method_get_return_type;
    api.method_get_param_count = fake_method_get_param_count;
    api.method_get_param = fake_method_get_param;
    api.method_get_param_name = fake_method_get_param_name;
    api.field_get_name = fake_field_get_name;
    api.field_get_type = fake_field_get_type;
    api.type_get_name = fake_type_get_name;
    api.free_memory = fake_free;
    return api;
}

bool check_unity_overload_resolution() {
    bool ok = true;
    engines::unity::Il2CppResolver resolver(fake_api());

    std::vector<engines::unity::Il2CppImageView> images;
    auto result = resolver.images(&images);
    ok = expect_runtime_status("unity fake image list", result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("unity fake image present", images.size() == 1) && ok;

    engines::unity::Il2CppImageView image;
    result = resolver.find_image({"Assembly-CSharp.dll"}, &image);
    ok = expect_runtime_status("unity fake image query", result.status, runtime::RuntimeStatus::Ok) && ok;

    engines::unity::Il2CppClassView klass;
    result = resolver.find_class({image.image, "Game", "Player"}, &klass);
    ok = expect_runtime_status("unity fake class query", result.status, runtime::RuntimeStatus::Ok) && ok;

    engines::unity::Il2CppMethodView method;
    result = resolver.find_method({klass.klass, "Tick", 1, {"System.Int32"}}, &method);
    ok = expect_runtime_status("unity overload int query", result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true(
        "unity overload int handle",
        method.method == reinterpret_cast<const engines::unity::MethodInfo*>(&kTickInt)
    ) && ok;
    ok = expect_true("unity overload int return", method.return_type == "System.Int32") && ok;

    result = resolver.find_method({klass.klass, "Tick", 1, {"System.Int32"}}, &method);
    ok = expect_runtime_status("unity overload int cache query", result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true("unity overload int cache hit", resolver.cache_stats().method_hits == 1) && ok;

    result = resolver.find_method({klass.klass, "Tick", 1, {"System.Single"}}, &method);
    ok = expect_runtime_status("unity overload single query", result.status, runtime::RuntimeStatus::Ok) && ok;
    ok = expect_true(
        "unity overload single handle",
        method.method == reinterpret_cast<const engines::unity::MethodInfo*>(&kTickSingle)
    ) && ok;
    ok = expect_true("unity overload single return", method.return_type == "System.Void") && ok;

    result = resolver.find_method({klass.klass, "Tick", 1, {"System.String"}}, &method);
    ok = expect_runtime_status("unity overload wrong signature", result.status, runtime::RuntimeStatus::NotFound) && ok;

    return ok;
}

bool check_sdk_engine_contract() {
    bool ok = true;

    const auto unity_probe = engine::IsUnity();
    ok = expect_sdk_status(
        "sdk unity probe unavailable without libil2cpp",
        unity_probe.status,
        Status::Unavailable
    ) && ok;

    const auto unreal_probe = engine::IsUnreal();
    ok = expect_true(
        "sdk unreal probe is structured",
        unreal_probe.status == Status::Ok || unreal_probe.status == Status::Unavailable
    ) && ok;

    std::vector<engine::Image> images;
    const auto images_result = engine::GetImages(&images);
    ok = expect_sdk_status(
        "sdk unity images unavailable without libil2cpp",
        images_result.status,
        Status::Unavailable
    ) && ok;
    ok = expect_true("sdk unity image list is empty when unavailable", images.empty()) && ok;

    engine::Image image;
    const auto image_result = engine::FindImage("Assembly-CSharp.dll", &image);
    ok = expect_sdk_status(
        "sdk unity image query unavailable without libil2cpp",
        image_result.status,
        Status::Unavailable
    ) && ok;
    ok = expect_true("sdk unity image query keeps empty handle", image.handle == nullptr) && ok;

    const auto path_method = engine::TryFindMethod("Assembly-CSharp.dll::Game.Player::Tick(1)");
    ok = expect_sdk_status(
        "sdk unity method path unavailable without libil2cpp",
        path_method.result.status,
        Status::Unavailable
    ) && ok;
    ok = expect_true("sdk unity method path keeps empty handle", path_method.value.handle == nullptr) && ok;
    ok = expect_true(
        "sdk unity direct method path returns null when unavailable",
        engine::FindMethod("Assembly-CSharp.dll::Game.Player::Tick(1)") == nullptr
    ) && ok;

    const auto invalid_path = engine::TryFindMethod("Assembly-CSharp.dll::Broken");
    ok = expect_sdk_status(
        "sdk unity method path validates shape",
        invalid_path.result.status,
        Status::InvalidArgument
    ) && ok;

    const char* params[] = {"System.Single"};
    engine::Method method;
    engine::ClearCache();
    engine::ClearEvents();
    const auto method_result = engine::BindMethod(
        engine::MethodSignature{
            "Assembly-CSharp.dll",
            "",
            "Player",
            "Tick",
            "System.Void",
            params,
            1
        },
        &method
    );
    ok = expect_sdk_status(
        "sdk unity method verification unavailable without libil2cpp",
        method_result.status,
        Status::Unavailable
    ) && ok;

    std::vector<engine::BindingEvent> events;
    const auto events_result = engine::GetEvents(&events);
    ok = expect_sdk_status("sdk unity binding events query succeeds", events_result.status, Status::Ok) && ok;
    ok = expect_true("sdk unity binding event records failure", !events.empty()) && ok;

    engine::ClearEvents();
    for (int i = 0; i < 132; ++i) {
        (void)engine::BindMethod(
            engine::MethodSignature{
                "Assembly-CSharp.dll",
                "",
                "Player",
                "Tick",
                "System.Void",
                params,
                1
            },
            &method
        );
    }
    events.clear();
    (void)engine::GetEvents(&events);
    ok = expect_true("sdk unity binding events are capped", events.size() == 128) && ok;
    ok = expect_true("sdk unity binding dropped events counted", engine::DroppedEvents() == 4) && ok;

    engine::Field field;
    const auto invalid_field = engine::BindField(engine::FieldSignature{}, &field);
    ok = expect_sdk_status(
        "sdk unity field signature validates input",
        invalid_field.status,
        Status::InvalidArgument
    ) && ok;

    return ok;
}

} // namespace

bool CheckEngine() {
    engines::unity::Il2CppResolver unity;
    engines::unreal::UeRuntime unreal;

    const auto unity_probe = unity.probe();
    const auto unreal_probe = unreal.probe();

    bool ok = true;
    ok = expect_status(
        "unity resolver unavailable without libil2cpp",
        unity_probe.status,
        engines::EngineStatus::Unavailable
    ) && ok;

    std::vector<engines::unity::Il2CppImageView> images;
    const auto images_result = unity.images(&images);
    ok = expect_runtime_status(
        "unity images unavailable without libil2cpp",
        images_result.status,
        runtime::RuntimeStatus::Unavailable
    ) && ok;
    ok = expect_empty("unity unavailable image list", images) && ok;
    ok = expect_not_failed("unreal probe", unreal_probe) && ok;
    ok = check_unity_overload_resolution() && ok;
    ok = check_sdk_engine_contract() && ok;

    NYX_LOGI("engine doctor %s", ok ? "passed" : "failed");
    return ok;
}

} // namespace test
} // namespace sdk
} // namespace nyx
