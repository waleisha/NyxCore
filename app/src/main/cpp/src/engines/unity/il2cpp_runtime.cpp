#include "src/engines/unity/il2cpp_runtime.h"

#include "sdk/include/utils.h"

#include <dlfcn.h>

namespace nyx {
namespace engines {
namespace unity {

namespace {

constexpr const char* kIl2CppLibrary = "libil2cpp.so";

template <typename Func>
bool find_export(void* handle, const char* name, Func* out, std::string* missing) {
    if (handle == nullptr || name == nullptr || out == nullptr) {
        return false;
    }

    dlerror();
    void* address = dlsym(handle, name);
    const char* error = dlerror();
    if (error != nullptr || address == nullptr) {
        if (missing != nullptr) {
            *missing = name;
        }
        return false;
    }

    *out = reinterpret_cast<Func>(address);
    return true;
}

template <typename Func>
void find_optional_export(void* handle, const char* name, Func* out) {
    if (handle == nullptr || name == nullptr || out == nullptr) {
        return;
    }

    dlerror();
    void* address = dlsym(handle, name);
    const char* error = dlerror();
    if (error == nullptr && address != nullptr) {
        *out = reinterpret_cast<Func>(address);
    }
}

runtime::RuntimeResult missing_export(const std::string& name) {
    const std::string detail = name.empty() ? "missing il2cpp export" : "missing il2cpp export: " + name;
    NYX_LOGW("unity il2cpp runtime %s", detail.c_str());
    return runtime::RuntimeResult{runtime::RuntimeStatus::NotFound, detail};
}

} // namespace

bool Il2CppApi::ready() const {
    return domain_get != nullptr &&
        domain_get_assemblies != nullptr &&
        assembly_get_image != nullptr &&
        image_get_name != nullptr &&
        class_from_name != nullptr &&
        class_get_name != nullptr &&
        class_get_namespace != nullptr &&
        class_get_method_from_name != nullptr &&
        class_get_field_from_name != nullptr &&
        method_get_name != nullptr &&
        method_get_return_type != nullptr &&
        method_get_param_count != nullptr &&
        method_get_param != nullptr &&
        method_get_param_name != nullptr &&
        field_get_name != nullptr &&
        field_get_type != nullptr &&
        type_get_name != nullptr &&
        free_memory != nullptr;
}

EngineProbe Il2CppRuntime::probe() const {
    EngineProbe probe;
    probe.kind = EngineKind::Unity;

    Il2CppApi api;
    const auto result = symbols(&api);
    if (result.status == runtime::RuntimeStatus::Disabled) {
        probe.status = EngineStatus::Disabled;
        probe.detail = result.detail;
        return probe;
    }
    if (result.status == runtime::RuntimeStatus::Unavailable) {
        probe.status = EngineStatus::Unavailable;
        probe.detail = result.detail;
        return probe;
    }
    if (!result.ok()) {
        probe.status = EngineStatus::Failed;
        probe.detail = result.detail;
        return probe;
    }

    probe.status = EngineStatus::Available;
    probe.detail = "libil2cpp.so is loaded and required exports are resolved";
    return probe;
}

runtime::RuntimeResult Il2CppRuntime::symbols(Il2CppApi* out) const {
    if (out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp api output"};
    }

    *out = Il2CppApi{};

    dlerror();
    void* handle = dlopen(kIl2CppLibrary, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        const char* error = dlerror();
        const std::string detail = error != nullptr ? error : "libil2cpp.so is not loaded";
        NYX_LOGI("unity il2cpp runtime unavailable: %s", detail.c_str());
        return runtime::RuntimeResult{runtime::RuntimeStatus::Unavailable, detail};
    }

    Il2CppApi api;
    std::string missing;
    const bool resolved =
        find_export(handle, "il2cpp_domain_get", &api.domain_get, &missing) &&
        find_export(handle, "il2cpp_domain_get_assemblies", &api.domain_get_assemblies, &missing) &&
        find_export(handle, "il2cpp_assembly_get_image", &api.assembly_get_image, &missing) &&
        find_export(handle, "il2cpp_image_get_name", &api.image_get_name, &missing) &&
        find_export(handle, "il2cpp_class_from_name", &api.class_from_name, &missing) &&
        find_export(handle, "il2cpp_class_get_name", &api.class_get_name, &missing) &&
        find_export(handle, "il2cpp_class_get_namespace", &api.class_get_namespace, &missing) &&
        find_export(handle, "il2cpp_class_get_method_from_name", &api.class_get_method_from_name, &missing) &&
        find_export(handle, "il2cpp_class_get_field_from_name", &api.class_get_field_from_name, &missing) &&
        find_export(handle, "il2cpp_method_get_name", &api.method_get_name, &missing) &&
        find_export(handle, "il2cpp_method_get_return_type", &api.method_get_return_type, &missing) &&
        find_export(handle, "il2cpp_method_get_param_count", &api.method_get_param_count, &missing) &&
        find_export(handle, "il2cpp_method_get_param", &api.method_get_param, &missing) &&
        find_export(handle, "il2cpp_method_get_param_name", &api.method_get_param_name, &missing) &&
        find_export(handle, "il2cpp_field_get_name", &api.field_get_name, &missing) &&
        find_export(handle, "il2cpp_field_get_type", &api.field_get_type, &missing) &&
        find_export(handle, "il2cpp_type_get_name", &api.type_get_name, &missing) &&
        find_export(handle, "il2cpp_free", &api.free_memory, &missing);

    find_optional_export(handle, "il2cpp_class_get_methods", &api.class_get_methods);

    dlclose(handle);

    if (!resolved || !api.ready()) {
        return missing_export(missing);
    }

    *out = api;
    NYX_LOGI("unity il2cpp runtime exports resolved");
    return runtime::RuntimeResult{};
}

} // namespace unity
} // namespace engines
} // namespace nyx
