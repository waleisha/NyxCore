#pragma once

#include <string>
#include <vector>

#include "src/engines/unity/il2cpp_resolver.h"

namespace nyx {
namespace engines {
namespace unity {

struct MethodSignature {
    std::string image_name;
    std::string class_namespace;
    std::string class_name;
    std::string name;
    std::string return_type;
    std::vector<std::string> params;

    int arg_count() const {
        return static_cast<int>(params.size());
    }
};

struct FieldSignature {
    std::string image_name;
    std::string class_namespace;
    std::string class_name;
    std::string name;
    std::string type_name;
};

bool valid(const MethodSignature& signature, std::string* detail = nullptr);
bool valid(const FieldSignature& signature, std::string* detail = nullptr);
bool matches(const MethodSignature& signature, const Il2CppMethodView& method, std::string* detail = nullptr);
bool matches(const FieldSignature& signature, const Il2CppFieldView& field, std::string* detail = nullptr);

} // namespace unity
} // namespace engines
} // namespace nyx
