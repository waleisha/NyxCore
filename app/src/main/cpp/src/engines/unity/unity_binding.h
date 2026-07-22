#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "src/engines/base/engine_status.h"
#include "src/engines/unity/il2cpp_resolver.h"
#include "src/engines/unity/unity_signature.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace engines {
namespace unity {

enum class BindingKind {
    Method,
    Field,
};

const char* kind_name(BindingKind kind);

struct BindingEvent {
    BindingKind kind = BindingKind::Method;
    std::string image_name;
    std::string class_namespace;
    std::string class_name;
    std::string member_name;
    std::string return_type;
    std::string field_type;
    std::vector<std::string> params;
    runtime::RuntimeStatus status = runtime::RuntimeStatus::Ok;
    std::string detail;
    Il2CppCacheStats cache_stats;
};

class VerifiedMethod {
public:
    bool valid() const;
    const MethodSignature& signature() const;
    const MethodInfo* handle() const;
    const std::string& name() const;
    const std::string& return_type() const;
    const std::vector<Il2CppParamView>& params() const;

private:
    friend class UnityBinding;

    MethodSignature signature_;
    Il2CppMethodView view_;
};

class VerifiedField {
public:
    bool valid() const;
    const FieldSignature& signature() const;
    FieldInfo* handle() const;
    const std::string& name() const;
    const std::string& type_name() const;

private:
    friend class UnityBinding;

    FieldSignature signature_;
    Il2CppFieldView view_;
};

class UnityBinding {
public:
    EngineProbe probe() const;
    runtime::RuntimeResult method(const MethodSignature& signature, VerifiedMethod* out) const;
    runtime::RuntimeResult field(const FieldSignature& signature, VerifiedField* out) const;
    Il2CppCacheStats cache_stats() const;
    runtime::RuntimeResult events(std::vector<BindingEvent>* out) const;
    std::size_t dropped_events() const;
    void clear_events() const;
    void clear_cache() const;
    void record(BindingEvent event) const;

private:
    Il2CppResolver resolver_;
    mutable std::mutex events_mutex_;
    mutable std::vector<BindingEvent> events_;
    mutable std::size_t event_head_ = 0;
    mutable std::size_t dropped_events_ = 0;
};

} // namespace unity
} // namespace engines
} // namespace nyx
