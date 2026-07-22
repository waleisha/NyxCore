#include "sdk/include/engine.h"

#include "src/engines/base/engine_status.h"
#include "src/engines/unity/il2cpp_resolver.h"
#include "src/engines/unity/unity_binding.h"
#include "src/engines/unreal/ue_runtime.h"
#include "sdk/result_bridge.h"

#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace nyx {
namespace sdk {
namespace engine {

namespace {

constexpr std::size_t kMaxSdkMethodParams = 256;

struct MethodPath {
    std::string image;
    std::string name_space;
    std::string klass;
    std::string method;
    int arg_count = -1;
};

struct MethodInfoHead {
    void* method_pointer = nullptr;
};

Status status_from(engines::EngineStatus status) {
    switch (status) {
        case engines::EngineStatus::Available:
            return Status::Ok;
        case engines::EngineStatus::Disabled:
            return Status::Disabled;
        case engines::EngineStatus::Unavailable:
            return Status::Unavailable;
        case engines::EngineStatus::Failed:
            return Status::Failed;
    }

    return Status::Failed;
}

Probe probe_from(const engines::EngineProbe& probe) {
    const Kind kind = probe.kind == engines::EngineKind::Unreal ? Kind::Unreal : Kind::Unity;
    return Probe{kind, status_from(probe.status), probe.detail};
}

Result invalid(const std::string& detail) {
    return Result{Status::InvalidArgument, detail};
}

engines::unity::Il2CppResolver& resolver() {
    static engines::unity::Il2CppResolver value;
    return value;
}

engines::unity::UnityBinding& binding() {
    static engines::unity::UnityBinding value;
    return value;
}

bool text_empty(const char* text) {
    return text == nullptr || text[0] == '\0';
}

bool text_empty(const std::string& text) {
    return text.empty();
}

std::string text_or_empty(const char* text) {
    return text != nullptr ? text : "";
}

void record_method_event(
    const char* image,
    const char* name_space,
    const char* klass,
    const char* method,
    const Result& result
) {
    engines::unity::BindingEvent event;
    event.kind = engines::unity::BindingKind::Method;
    event.image_name = text_or_empty(image);
    event.class_namespace = text_or_empty(name_space);
    event.class_name = text_or_empty(klass);
    event.member_name = text_or_empty(method);
    event.status = bridge::runtime_status_from(result.status);
    event.detail = result.detail;
    binding().record(std::move(event));
}

bool parse_arg_count(const std::string& text, int* out) {
    if (out == nullptr || text.empty()) {
        return false;
    }
    if (text == "*") {
        *out = -1;
        return true;
    }

    int value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const int digit = ch - '0';
        if (value > (std::numeric_limits<int>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }

    *out = value;
    return true;
}

bool parse_method_path(const char* path, MethodPath* out, std::string* detail) {
    if (text_empty(path) || out == nullptr) {
        if (detail != nullptr) {
            *detail = "missing unity method path";
        }
        return false;
    }

    const std::string text(path);
    const std::size_t first = text.find("::");
    const std::size_t last = text.rfind("::");
    if (first == std::string::npos || last == std::string::npos || first == last) {
        if (detail != nullptr) {
            *detail = "unity method path must be image::class::method";
        }
        return false;
    }

    MethodPath parsed;
    parsed.image = text.substr(0, first);
    const std::string type = text.substr(first + 2, last - first - 2);
    std::string member = text.substr(last + 2);
    if (text_empty(parsed.image) || text_empty(type) || text_empty(member)) {
        if (detail != nullptr) {
            *detail = "unity method path contains an empty segment";
        }
        return false;
    }

    const std::size_t args = member.rfind('(');
    if (args != std::string::npos) {
        if (member.empty() || member.back() != ')') {
            if (detail != nullptr) {
                *detail = "unity method path argument list is malformed";
            }
            return false;
        }

        const std::string arg_text = member.substr(args + 1, member.size() - args - 2);
        member = member.substr(0, args);
        if (!parse_arg_count(arg_text, &parsed.arg_count)) {
            if (detail != nullptr) {
                *detail = "unity method path argument count is invalid";
            }
            return false;
        }
    }

    if (text_empty(member)) {
        if (detail != nullptr) {
            *detail = "unity method path is missing method name";
        }
        return false;
    }

    const std::size_t dot = type.rfind('.');
    if (dot == std::string::npos) {
        parsed.klass = type;
    } else {
        parsed.name_space = type.substr(0, dot);
        parsed.klass = type.substr(dot + 1);
    }
    parsed.method = member;

    if (text_empty(parsed.klass)) {
        if (detail != nullptr) {
            *detail = "unity method path is missing class name";
        }
        return false;
    }

    *out = std::move(parsed);
    return true;
}

bool fill_params(const MethodQuery& query, std::vector<std::string>* out, std::string* detail) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    if (query.param_count > kMaxSdkMethodParams) {
        if (detail != nullptr) {
            *detail = "unity method parameter count is too large";
        }
        return false;
    }
    if (query.param_count == 0) {
        return true;
    }
    if (query.params == nullptr) {
        if (detail != nullptr) {
            *detail = "missing unity method parameter list";
        }
        return false;
    }

    out->reserve(query.param_count);
    for (std::size_t i = 0; i < query.param_count; ++i) {
        if (text_empty(query.params[i])) {
            if (detail != nullptr) {
                *detail = "missing unity method parameter type";
            }
            return false;
        }
        out->push_back(query.params[i]);
    }
    return true;
}

bool fill_signature(
    const MethodSignature& signature,
    engines::unity::MethodSignature* out,
    std::string* detail
) {
    if (out == nullptr) {
        return false;
    }
    if (text_empty(signature.image_name) ||
        text_empty(signature.class_name) ||
        text_empty(signature.name) ||
        text_empty(signature.return_type)) {
        if (detail != nullptr) {
            *detail = "missing unity method signature field";
        }
        return false;
    }
    if (signature.param_count > kMaxSdkMethodParams) {
        if (detail != nullptr) {
            *detail = "unity method parameter count is too large";
        }
        return false;
    }
    if (signature.param_count > 0 && signature.params == nullptr) {
        if (detail != nullptr) {
            *detail = "missing unity method parameter list";
        }
        return false;
    }

    out->image_name = signature.image_name;
    out->class_namespace = signature.class_namespace != nullptr ? signature.class_namespace : "";
    out->class_name = signature.class_name;
    out->name = signature.name;
    out->return_type = signature.return_type;
    out->params.clear();
    out->params.reserve(signature.param_count);
    for (std::size_t i = 0; i < signature.param_count; ++i) {
        if (text_empty(signature.params[i])) {
            if (detail != nullptr) {
                *detail = "missing unity method parameter type";
            }
            return false;
        }
        out->params.push_back(signature.params[i]);
    }

    return true;
}

bool fill_signature(
    const FieldSignature& signature,
    engines::unity::FieldSignature* out,
    std::string* detail
) {
    if (out == nullptr) {
        return false;
    }
    if (text_empty(signature.image_name) ||
        text_empty(signature.class_name) ||
        text_empty(signature.name) ||
        text_empty(signature.type_name)) {
        if (detail != nullptr) {
            *detail = "missing unity field signature field";
        }
        return false;
    }

    out->image_name = signature.image_name;
    out->class_namespace = signature.class_namespace != nullptr ? signature.class_namespace : "";
    out->class_name = signature.class_name;
    out->name = signature.name;
    out->type_name = signature.type_name;
    return true;
}

Image image_from(const engines::unity::Il2CppImageView& image) {
    return Image{image.image, image.name};
}

Class class_from(const engines::unity::Il2CppClassView& klass) {
    return Class{klass.klass, klass.class_namespace, klass.name};
}

Method method_from(const engines::unity::Il2CppMethodView& method) {
    Method out;
    out.handle = method.method;
    out.name = method.name;
    out.arg_count = method.arg_count;
    out.return_type = method.return_type;
    out.params.reserve(method.params.size());
    for (const auto& param : method.params) {
        out.params.push_back(param.type_name);
    }
    return out;
}

Method method_from(const engines::unity::VerifiedMethod& method) {
    Method out;
    out.handle = method.handle();
    out.name = method.name();
    out.arg_count = static_cast<int>(method.params().size());
    out.return_type = method.return_type();
    out.params.reserve(method.params().size());
    for (const auto& param : method.params()) {
        out.params.push_back(param.type_name);
    }
    return out;
}

Field field_from(const engines::unity::Il2CppFieldView& field) {
    return Field{field.field, field.name, field.type_name};
}

Field field_from(const engines::unity::VerifiedField& field) {
    return Field{field.handle(), field.name(), field.type_name()};
}

BindingEvent event_from(const engines::unity::BindingEvent& event) {
    return BindingEvent{
        engines::unity::kind_name(event.kind),
        event.image_name,
        event.class_namespace,
        event.class_name,
        event.member_name,
        bridge::status_from(event.status),
        event.detail
    };
}

} // namespace

Probe IsUnity() {
    return probe_from(resolver().probe());
}

Probe IsUnreal() {
    engines::unreal::UeRuntime unreal;
    return probe_from(unreal.probe());
}

Result GetImages(std::vector<Image>* out) {
    if (out == nullptr) {
        return invalid("missing unity image output");
    }

    std::vector<engines::unity::Il2CppImageView> images;
    const auto result = resolver().images(&images);
    out->clear();
    out->reserve(images.size());
    for (const auto& image : images) {
        out->push_back(image_from(image));
    }
    return bridge::result_from(result);
}

Result FindImage(const char* name, Image* out) {
    if (out != nullptr) {
        *out = Image{};
    }
    if (text_empty(name) || out == nullptr) {
        return invalid("missing unity image query");
    }

    engines::unity::Il2CppImageView image;
    const auto result = resolver().find_image(engines::unity::Il2CppImageQuery{name}, &image);
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = image_from(image);
    return bridge::result_from(result);
}

Result FindClass(const ClassQuery& query, Class* out) {
    if (out != nullptr) {
        *out = Class{};
    }
    if (query.image == nullptr || text_empty(query.name) || out == nullptr) {
        return invalid("missing unity class query");
    }

    engines::unity::Il2CppClassView klass;
    const auto result = resolver().find_class(
        engines::unity::Il2CppClassQuery{
            static_cast<const engines::unity::Il2CppImage*>(query.image),
            query.name_space != nullptr ? query.name_space : "",
            query.name
        },
        &klass
    );
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = class_from(klass);
    return bridge::result_from(result);
}

Result FindMethod(const MethodQuery& query, Method* out) {
    if (out != nullptr) {
        *out = Method{};
    }
    if (query.klass == nullptr || text_empty(query.name) || out == nullptr) {
        return invalid("missing unity method query");
    }

    std::vector<std::string> params;
    std::string detail;
    if (!fill_params(query, &params, &detail)) {
        return invalid(detail);
    }
    if (query.arg_count >= 0 &&
        !params.empty() &&
        query.arg_count != static_cast<int>(params.size())) {
        return invalid("unity method parameter count mismatch");
    }

    engines::unity::Il2CppMethodView method;
    const auto result = resolver().find_method(
        engines::unity::Il2CppMethodQuery{
            static_cast<engines::unity::Il2CppClass*>(query.klass),
            query.name,
            query.arg_count,
            std::move(params)
        },
        &method
    );
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = method_from(method);
    return bridge::result_from(result);
}

Value<Method> TryFindMethod(const char* path) {
    Value<Method> out;

    MethodPath parsed;
    std::string detail;
    if (!parse_method_path(path, &parsed, &detail)) {
        out.result = invalid(detail);
        record_method_event(nullptr, nullptr, nullptr, path, out.result);
        return out;
    }

    return TryFindMethod(
        parsed.image.c_str(),
        parsed.name_space.c_str(),
        parsed.klass.c_str(),
        parsed.method.c_str(),
        parsed.arg_count
    );
}

Value<Method> TryFindMethod(
    const char* image,
    const char* klass,
    const char* method,
    int arg_count
) {
    return TryFindMethod(image, "", klass, method, arg_count);
}

Value<Method> TryFindMethod(
    const char* image,
    const char* name_space,
    const char* klass,
    const char* method,
    int arg_count
) {
    Value<Method> out;
    if (text_empty(image) || text_empty(klass) || text_empty(method)) {
        out.result = invalid("missing unity method lookup field");
        record_method_event(image, name_space, klass, method, out.result);
        return out;
    }

    Image image_result;
    out.result = FindImage(image, &image_result);
    if (!out.result.ok()) {
        record_method_event(image, name_space, klass, method, out.result);
        return out;
    }

    Class class_result;
    out.result = FindClass(ClassQuery{
        image_result.handle,
        name_space != nullptr ? name_space : "",
        klass
    }, &class_result);
    if (!out.result.ok()) {
        record_method_event(image, name_space, klass, method, out.result);
        return out;
    }

    out.result = FindMethod(MethodQuery{class_result.handle, method, arg_count}, &out.value);
    record_method_event(image, name_space, klass, method, out.result);
    return out;
}

void* MethodPtr(const Method& method) {
    if (method.handle == nullptr) {
        return nullptr;
    }

    const auto* head = static_cast<const MethodInfoHead*>(method.handle);
    return head->method_pointer;
}

void* FindMethod(const char* path) {
    auto value = TryFindMethod(path);
    if (!value.ok()) {
        NYX_LOGW("engine FindMethod failed: %s", value.result.detail.c_str());
        return nullptr;
    }
    return MethodPtr(value.value);
}

void* FindMethod(const char* image, const char* klass, const char* method, int arg_count) {
    auto value = TryFindMethod(image, klass, method, arg_count);
    if (!value.ok()) {
        NYX_LOGW("engine FindMethod failed: %s", value.result.detail.c_str());
        return nullptr;
    }
    return MethodPtr(value.value);
}

void* FindMethod(
    const char* image,
    const char* name_space,
    const char* klass,
    const char* method,
    int arg_count
) {
    auto value = TryFindMethod(image, name_space, klass, method, arg_count);
    if (!value.ok()) {
        NYX_LOGW("engine FindMethod failed: %s", value.result.detail.c_str());
        return nullptr;
    }
    return MethodPtr(value.value);
}

Result FindField(const FieldQuery& query, Field* out) {
    if (out != nullptr) {
        *out = Field{};
    }
    if (query.klass == nullptr || text_empty(query.name) || out == nullptr) {
        return invalid("missing unity field query");
    }

    engines::unity::Il2CppFieldView field;
    const auto result = resolver().find_field(
        engines::unity::Il2CppFieldQuery{
            static_cast<engines::unity::Il2CppClass*>(query.klass),
            query.name
        },
        &field
    );
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = field_from(field);
    return bridge::result_from(result);
}

Result BindMethod(const MethodSignature& signature, Method* out) {
    if (out != nullptr) {
        *out = Method{};
    }
    if (out == nullptr) {
        return invalid("missing verified unity method output");
    }

    engines::unity::MethodSignature internal;
    std::string detail;
    if (!fill_signature(signature, &internal, &detail)) {
        return invalid(detail);
    }

    engines::unity::VerifiedMethod verified;
    const auto result = binding().method(internal, &verified);
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = method_from(verified);
    return bridge::result_from(result);
}

Result BindField(const FieldSignature& signature, Field* out) {
    if (out != nullptr) {
        *out = Field{};
    }
    if (out == nullptr) {
        return invalid("missing verified unity field output");
    }

    engines::unity::FieldSignature internal;
    std::string detail;
    if (!fill_signature(signature, &internal, &detail)) {
        return invalid(detail);
    }

    engines::unity::VerifiedField verified;
    const auto result = binding().field(internal, &verified);
    if (!result.ok()) {
        return bridge::result_from(result);
    }

    *out = field_from(verified);
    return bridge::result_from(result);
}

void ClearCache() {
    resolver().clear_cache();
    binding().clear_cache();
}

Result GetEvents(std::vector<BindingEvent>* out) {
    if (out == nullptr) {
        return invalid("missing unity binding events output");
    }

    std::vector<engines::unity::BindingEvent> events;
    const auto result = binding().events(&events);
    out->clear();
    out->reserve(events.size());
    for (const auto& event : events) {
        out->push_back(event_from(event));
    }
    return bridge::result_from(result);
}

void ClearEvents() {
    binding().clear_events();
}

std::size_t DroppedEvents() {
    return binding().dropped_events();
}

} // namespace engine
} // namespace sdk
} // namespace nyx
