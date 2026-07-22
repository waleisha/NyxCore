#include "src/engines/unity/unity_signature.h"

#include <cstddef>

namespace nyx {
namespace engines {
namespace unity {

namespace {

constexpr std::size_t kMaxMethodParams = 256;

bool fail(std::string* detail, const char* message) {
    if (detail != nullptr) {
        *detail = message != nullptr ? message : "unity signature mismatch";
    }
    return false;
}

bool empty(const std::string& text) {
    return text.empty();
}

} // namespace

bool valid(const MethodSignature& signature, std::string* detail) {
    if (empty(signature.image_name)) {
        return fail(detail, "missing unity method image");
    }
    if (empty(signature.class_name)) {
        return fail(detail, "missing unity method class");
    }
    if (empty(signature.name)) {
        return fail(detail, "missing unity method name");
    }
    if (empty(signature.return_type)) {
        return fail(detail, "missing unity method return type");
    }
    if (signature.params.size() > kMaxMethodParams) {
        return fail(detail, "unity method parameter count is too large");
    }
    for (const auto& param : signature.params) {
        if (empty(param)) {
            return fail(detail, "missing unity method parameter type");
        }
    }

    if (detail != nullptr) {
        detail->clear();
    }
    return true;
}

bool valid(const FieldSignature& signature, std::string* detail) {
    if (empty(signature.image_name)) {
        return fail(detail, "missing unity field image");
    }
    if (empty(signature.class_name)) {
        return fail(detail, "missing unity field class");
    }
    if (empty(signature.name)) {
        return fail(detail, "missing unity field name");
    }
    if (empty(signature.type_name)) {
        return fail(detail, "missing unity field type");
    }

    if (detail != nullptr) {
        detail->clear();
    }
    return true;
}

bool matches(const MethodSignature& signature, const Il2CppMethodView& method, std::string* detail) {
    if (!method.valid()) {
        return fail(detail, "unity method is not resolved");
    }
    if (method.name != signature.name) {
        return fail(detail, "unity method name mismatch");
    }
    if (method.return_type != signature.return_type) {
        return fail(detail, "unity method return type mismatch");
    }
    if (method.params.size() != signature.params.size()) {
        return fail(detail, "unity method parameter count mismatch");
    }
    for (std::size_t i = 0; i < signature.params.size(); ++i) {
        if (method.params[i].type_name != signature.params[i]) {
            return fail(detail, "unity method parameter type mismatch");
        }
    }

    if (detail != nullptr) {
        detail->clear();
    }
    return true;
}

bool matches(const FieldSignature& signature, const Il2CppFieldView& field, std::string* detail) {
    if (!field.valid()) {
        return fail(detail, "unity field is not resolved");
    }
    if (field.name != signature.name) {
        return fail(detail, "unity field name mismatch");
    }
    if (field.type_name != signature.type_name) {
        return fail(detail, "unity field type mismatch");
    }

    if (detail != nullptr) {
        detail->clear();
    }
    return true;
}

} // namespace unity
} // namespace engines
} // namespace nyx
