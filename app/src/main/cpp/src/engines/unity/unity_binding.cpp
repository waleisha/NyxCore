#include "src/engines/unity/unity_binding.h"

#include "sdk/include/utils.h"

#include <utility>

namespace nyx {
namespace engines {
namespace unity {

namespace {

constexpr std::size_t kMaxBindingEvents = 128;

runtime::RuntimeResult invalid(const std::string& detail) {
    return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, detail};
}

runtime::RuntimeResult denied(const std::string& detail) {
    return runtime::RuntimeResult{runtime::RuntimeStatus::Denied, detail};
}

BindingEvent event_from(const MethodSignature& signature) {
    BindingEvent event;
    event.kind = BindingKind::Method;
    event.image_name = signature.image_name;
    event.class_namespace = signature.class_namespace;
    event.class_name = signature.class_name;
    event.member_name = signature.name;
    event.return_type = signature.return_type;
    event.params = signature.params;
    return event;
}

BindingEvent event_from(const FieldSignature& signature) {
    BindingEvent event;
    event.kind = BindingKind::Field;
    event.image_name = signature.image_name;
    event.class_namespace = signature.class_namespace;
    event.class_name = signature.class_name;
    event.member_name = signature.name;
    event.field_type = signature.type_name;
    return event;
}

void fill_actual(BindingEvent* event, const Il2CppMethodView& method) {
    if (event == nullptr) {
        return;
    }

    event->return_type = method.return_type;
    event->params.clear();
    for (const auto& param : method.params) {
        event->params.push_back(param.type_name);
    }
}

void fill_actual(BindingEvent* event, const Il2CppFieldView& field) {
    if (event == nullptr) {
        return;
    }

    event->field_type = field.type_name;
}

} // namespace

const char* kind_name(BindingKind kind) {
    switch (kind) {
        case BindingKind::Method:
            return "method";
        case BindingKind::Field:
            return "field";
    }

    return "unknown";
}

bool VerifiedMethod::valid() const {
    return view_.valid();
}

const MethodSignature& VerifiedMethod::signature() const {
    return signature_;
}

const MethodInfo* VerifiedMethod::handle() const {
    return view_.method;
}

const std::string& VerifiedMethod::name() const {
    return view_.name;
}

const std::string& VerifiedMethod::return_type() const {
    return view_.return_type;
}

const std::vector<Il2CppParamView>& VerifiedMethod::params() const {
    return view_.params;
}

bool VerifiedField::valid() const {
    return view_.valid();
}

const FieldSignature& VerifiedField::signature() const {
    return signature_;
}

FieldInfo* VerifiedField::handle() const {
    return view_.field;
}

const std::string& VerifiedField::name() const {
    return view_.name;
}

const std::string& VerifiedField::type_name() const {
    return view_.type_name;
}

EngineProbe UnityBinding::probe() const {
    return resolver_.probe();
}

runtime::RuntimeResult UnityBinding::method(const MethodSignature& signature, VerifiedMethod* out) const {
    BindingEvent event = event_from(signature);

    if (out == nullptr) {
        auto result = invalid("missing verified unity method output");
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }
    *out = VerifiedMethod{};

    std::string detail;
    if (!valid(signature, &detail)) {
        auto result = invalid(detail);
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppImageView image;
    auto result = resolver_.find_image({signature.image_name}, &image);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppClassView klass;
    result = resolver_.find_class({image.image, signature.class_namespace, signature.class_name}, &klass);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppMethodView method;
    result = resolver_.find_method({klass.klass, signature.name, signature.arg_count(), signature.params}, &method);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }
    fill_actual(&event, method);

    if (!matches(signature, method, &detail)) {
        NYX_LOGD("unity binding denied method %s.%s: %s",
            signature.class_name.c_str(), signature.name.c_str(), detail.c_str());
        result = denied(detail);
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    out->signature_ = signature;
    out->view_ = method;
    NYX_LOGD("unity binding verified method: %s.%s", signature.class_name.c_str(), signature.name.c_str());
    event.status = runtime::RuntimeStatus::Ok;
    record(std::move(event));
    return runtime::RuntimeResult{};
}

runtime::RuntimeResult UnityBinding::field(const FieldSignature& signature, VerifiedField* out) const {
    BindingEvent event = event_from(signature);

    if (out == nullptr) {
        auto result = invalid("missing verified unity field output");
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }
    *out = VerifiedField{};

    std::string detail;
    if (!valid(signature, &detail)) {
        auto result = invalid(detail);
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppImageView image;
    auto result = resolver_.find_image({signature.image_name}, &image);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppClassView klass;
    result = resolver_.find_class({image.image, signature.class_namespace, signature.class_name}, &klass);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    Il2CppFieldView field;
    result = resolver_.find_field({klass.klass, signature.name}, &field);
    if (!result.ok()) {
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }
    fill_actual(&event, field);

    if (!matches(signature, field, &detail)) {
        NYX_LOGD("unity binding denied field %s.%s: %s",
            signature.class_name.c_str(), signature.name.c_str(), detail.c_str());
        result = denied(detail);
        event.status = result.status;
        event.detail = result.detail;
        record(std::move(event));
        return result;
    }

    out->signature_ = signature;
    out->view_ = field;
    NYX_LOGD("unity binding verified field: %s.%s", signature.class_name.c_str(), signature.name.c_str());
    event.status = runtime::RuntimeStatus::Ok;
    record(std::move(event));
    return runtime::RuntimeResult{};
}

Il2CppCacheStats UnityBinding::cache_stats() const {
    return resolver_.cache_stats();
}

runtime::RuntimeResult UnityBinding::events(std::vector<BindingEvent>* out) const {
    if (out == nullptr) {
        return invalid("missing unity binding events output");
    }

    std::lock_guard<std::mutex> lock(events_mutex_);
    out->clear();
    out->reserve(events_.size());
    if (events_.size() < kMaxBindingEvents) {
        *out = events_;
        return runtime::RuntimeResult{};
    }

    for (std::size_t i = 0; i < events_.size(); ++i) {
        out->push_back(events_[(event_head_ + i) % events_.size()]);
    }
    return runtime::RuntimeResult{};
}

std::size_t UnityBinding::dropped_events() const {
    std::lock_guard<std::mutex> lock(events_mutex_);
    return dropped_events_;
}

void UnityBinding::clear_events() const {
    std::lock_guard<std::mutex> lock(events_mutex_);
    events_.clear();
    event_head_ = 0;
    dropped_events_ = 0;
}

void UnityBinding::clear_cache() const {
    resolver_.clear_cache();
}

void UnityBinding::record(BindingEvent event) const {
    event.cache_stats = resolver_.cache_stats();
    std::lock_guard<std::mutex> lock(events_mutex_);
    if (events_.size() < kMaxBindingEvents) {
        events_.push_back(std::move(event));
        return;
    }

    events_[event_head_] = std::move(event);
    event_head_ = (event_head_ + 1) % events_.size();
    ++dropped_events_;
}

} // namespace unity
} // namespace engines
} // namespace nyx
