#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/engines/base/engine_status.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace engines {
namespace unity {

struct Il2CppDomain;
struct Il2CppAssembly;
struct Il2CppImage;
struct Il2CppClass;
struct Il2CppType;
struct MethodInfo;
struct FieldInfo;

struct Il2CppApi {
    using DomainGet = Il2CppDomain* (*)();
    using DomainGetAssemblies = const Il2CppAssembly** (*)(const Il2CppDomain*, std::size_t*);
    using AssemblyGetImage = const Il2CppImage* (*)(const Il2CppAssembly*);
    using ImageGetName = const char* (*)(const Il2CppImage*);
    using ClassFromName = Il2CppClass* (*)(const Il2CppImage*, const char*, const char*);
    using ClassGetName = const char* (*)(Il2CppClass*);
    using ClassGetNamespace = const char* (*)(Il2CppClass*);
    using ClassGetMethodFromName = const MethodInfo* (*)(Il2CppClass*, const char*, int);
    using ClassGetMethods = const MethodInfo* (*)(Il2CppClass*, void**);
    using ClassGetFieldFromName = FieldInfo* (*)(Il2CppClass*, const char*);
    using MethodGetName = const char* (*)(const MethodInfo*);
    using MethodGetReturnType = const Il2CppType* (*)(const MethodInfo*);
    using MethodGetParamCount = std::uint32_t (*)(const MethodInfo*);
    using MethodGetParam = const Il2CppType* (*)(const MethodInfo*, std::uint32_t);
    using MethodGetParamName = const char* (*)(const MethodInfo*, std::uint32_t);
    using FieldGetName = const char* (*)(FieldInfo*);
    using FieldGetType = const Il2CppType* (*)(FieldInfo*);
    using TypeGetName = char* (*)(const Il2CppType*);
    using Free = void (*)(void*);

    DomainGet domain_get = nullptr;
    DomainGetAssemblies domain_get_assemblies = nullptr;
    AssemblyGetImage assembly_get_image = nullptr;
    ImageGetName image_get_name = nullptr;
    ClassFromName class_from_name = nullptr;
    ClassGetName class_get_name = nullptr;
    ClassGetNamespace class_get_namespace = nullptr;
    ClassGetMethodFromName class_get_method_from_name = nullptr;
    ClassGetMethods class_get_methods = nullptr;
    ClassGetFieldFromName class_get_field_from_name = nullptr;
    MethodGetName method_get_name = nullptr;
    MethodGetReturnType method_get_return_type = nullptr;
    MethodGetParamCount method_get_param_count = nullptr;
    MethodGetParam method_get_param = nullptr;
    MethodGetParamName method_get_param_name = nullptr;
    FieldGetName field_get_name = nullptr;
    FieldGetType field_get_type = nullptr;
    TypeGetName type_get_name = nullptr;
    Free free_memory = nullptr;

    bool ready() const;
};

class Il2CppRuntime {
public:
    EngineProbe probe() const;
    runtime::RuntimeResult symbols(Il2CppApi* out) const;
};

} // namespace unity
} // namespace engines
} // namespace nyx
