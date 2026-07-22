#include "src/engines/unity/il2cpp_resolver.h"

#include "sdk/include/utils.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace nyx {
namespace engines {
namespace unity {

namespace {

constexpr std::size_t kMaxMethodParams = 256;

std::string safe_text(const char* text) {
    return text != nullptr ? text : "";
}

runtime::RuntimeResult not_found(const char* what) {
    return runtime::RuntimeResult{runtime::RuntimeStatus::NotFound, what != nullptr ? what : "il2cpp item not found"};
}

void append_key_part(std::string* key, const std::string& value) {
    if (key == nullptr) {
        return;
    }

    key->append(std::to_string(value.size()));
    key->push_back(':');
    key->append(value);
    key->push_back('\n');
}

void append_key_part(std::string* key, const char* value) {
    append_key_part(key, std::string(value != nullptr ? value : ""));
}

void append_pointer_part(std::string* key, const void* pointer) {
    append_key_part(key, std::to_string(reinterpret_cast<std::uintptr_t>(pointer)));
}

std::string class_key(const Il2CppClassQuery& query) {
    std::string key;
    append_pointer_part(&key, query.image);
    append_key_part(&key, query.class_namespace);
    append_key_part(&key, query.name);
    return key;
}

std::string method_key(const Il2CppMethodQuery& query) {
    std::string key;
    append_pointer_part(&key, query.klass);
    append_key_part(&key, query.name);
    append_key_part(&key, std::to_string(query.arg_count));
    append_key_part(&key, std::to_string(query.params.size()));
    for (const auto& param : query.params) {
        append_key_part(&key, param);
    }
    return key;
}

std::string field_key(const Il2CppFieldQuery& query) {
    std::string key;
    append_pointer_part(&key, query.klass);
    append_key_part(&key, query.name);
    return key;
}

std::string type_name(const Il2CppApi& api, const Il2CppType* type) {
    if (type == nullptr || api.type_get_name == nullptr) {
        return {};
    }

    char* name = api.type_get_name(type);
    std::string out = safe_text(name);
    if (name != nullptr && api.free_memory != nullptr) {
        api.free_memory(name);
    }
    return out;
}

runtime::RuntimeResult fill_method(const Il2CppApi& api, const MethodInfo* method, Il2CppMethodView* out) {
    if (method == nullptr || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp method view"};
    }

    out->method = method;
    out->name = safe_text(api.method_get_name(method));
    out->return_type = type_name(api, api.method_get_return_type(method));
    if (out->return_type.empty()) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp method return type is empty"};
    }

    const std::uint32_t count = api.method_get_param_count(method);
    if (count > kMaxMethodParams) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp method parameter count is too large"};
    }

    out->arg_count = static_cast<int>(count);
    out->params.clear();
    out->params.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Il2CppParamView param;
        param.name = safe_text(api.method_get_param_name(method, i));
        param.type_name = type_name(api, api.method_get_param(method, i));
        if (param.type_name.empty()) {
            return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp method parameter type is empty"};
        }
        out->params.push_back(param);
    }

    return runtime::RuntimeResult{};
}

runtime::RuntimeResult fill_field(const Il2CppApi& api, FieldInfo* field, Il2CppFieldView* out) {
    if (field == nullptr || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp field view"};
    }

    out->field = field;
    out->name = safe_text(api.field_get_name(field));
    out->type_name = type_name(api, api.field_get_type(field));
    if (out->type_name.empty()) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp field type is empty"};
    }

    return runtime::RuntimeResult{};
}

bool method_matches_query(const Il2CppMethodQuery& query, const Il2CppMethodView& method) {
    if (!method.valid() || method.name != query.name) {
        return false;
    }

    if (query.arg_count >= 0 && method.arg_count != query.arg_count) {
        return false;
    }

    if (query.params.empty()) {
        return true;
    }

    if (method.params.size() != query.params.size()) {
        return false;
    }

    for (std::size_t i = 0; i < query.params.size(); ++i) {
        if (method.params[i].type_name != query.params[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

Il2CppResolver::Il2CppResolver(const Il2CppApi& api)
    : api_cached_(api.ready()),
      api_cache_(api) {}

EngineProbe Il2CppResolver::probe() const {
    return runtime_.probe();
}

runtime::RuntimeResult Il2CppResolver::api(Il2CppApi* out) const {
    if (out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp api output"};
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (api_cached_) {
            *out = api_cache_;
            return runtime::RuntimeResult{};
        }
    }

    Il2CppApi resolved;
    const auto result = runtime_.symbols(&resolved);
    if (!result.ok()) {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        api_cache_ = resolved;
        api_cached_ = true;
    }

    *out = resolved;
    return runtime::RuntimeResult{};
}

runtime::RuntimeResult Il2CppResolver::images(std::vector<Il2CppImageView>* out) const {
    if (out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp image output"};
    }

    out->clear();

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (images_cached_) {
            *out = image_cache_;
            NYX_LOGD("unity il2cpp resolver image list cache hit: %zu", out->size());
            return runtime::RuntimeResult{};
        }
    }

    Il2CppApi api;
    const auto symbols = this->api(&api);
    if (!symbols.ok()) {
        return symbols;
    }

    Il2CppDomain* domain = api.domain_get();
    if (domain == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp domain is null"};
    }

    std::size_t count = 0;
    const Il2CppAssembly** assemblies = api.domain_get_assemblies(domain, &count);
    if (assemblies == nullptr && count != 0) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::Failed, "il2cpp assemblies pointer is null"};
    }

    std::vector<Il2CppImageView> list;
    list.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Il2CppAssembly* assembly = assemblies[i];
        if (assembly == nullptr) {
            NYX_LOGW("unity il2cpp resolver skipped null assembly at index %zu", i);
            continue;
        }

        const Il2CppImage* image = api.assembly_get_image(assembly);
        if (image == nullptr) {
            NYX_LOGW("unity il2cpp resolver skipped assembly without image at index %zu", i);
            continue;
        }

        list.push_back(Il2CppImageView{image, safe_text(api.image_get_name(image))});
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        image_cache_ = list;
        image_by_name_.clear();
        for (const auto& image : image_cache_) {
            image_by_name_[image.name] = image;
        }
        images_cached_ = true;
    }

    *out = list;
    NYX_LOGD("unity il2cpp resolver listed %zu images", out->size());
    return runtime::RuntimeResult{};
}

runtime::RuntimeResult Il2CppResolver::find_image(const Il2CppImageQuery& query, Il2CppImageView* out) const {
    if (out != nullptr) {
        *out = Il2CppImageView{};
    }
    if (query.image_name.empty() || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp image query"};
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        const auto it = image_by_name_.find(query.image_name);
        if (it != image_by_name_.end()) {
            *out = it->second;
            ++cache_stats_.image_hits;
            NYX_LOGD("unity il2cpp resolver image cache hit");
            return runtime::RuntimeResult{};
        }
    }

    std::vector<Il2CppImageView> list;
    const auto result = images(&list);
    if (!result.ok()) {
        return result;
    }

    for (const auto& image : list) {
        if (image.name == query.image_name) {
            *out = image;
            NYX_LOGD("unity il2cpp resolver found image");
            return runtime::RuntimeResult{};
        }
    }

    return not_found("il2cpp image not found");
}

runtime::RuntimeResult Il2CppResolver::find_class(const Il2CppClassQuery& query, Il2CppClassView* out) const {
    if (out != nullptr) {
        *out = Il2CppClassView{};
    }
    if (query.image == nullptr || query.name.empty() || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp class query"};
    }

    const std::string key = class_key(query);

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        const auto it = class_cache_.find(key);
        if (it != class_cache_.end()) {
            *out = it->second;
            ++cache_stats_.class_hits;
            NYX_LOGD("unity il2cpp resolver class cache hit: %s.%s",
                out->class_namespace.c_str(), out->name.c_str());
            return runtime::RuntimeResult{};
        }
    }

    Il2CppApi api;
    const auto symbols = this->api(&api);
    if (!symbols.ok()) {
        return symbols;
    }

    Il2CppClass* klass = api.class_from_name(query.image, query.class_namespace.c_str(), query.name.c_str());
    if (klass == nullptr) {
        return not_found("il2cpp class not found");
    }

    out->klass = klass;
    out->class_namespace = safe_text(api.class_get_namespace(klass));
    out->name = safe_text(api.class_get_name(klass));
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        class_cache_[key] = *out;
    }
    NYX_LOGD("unity il2cpp resolver found class: %s.%s", out->class_namespace.c_str(), out->name.c_str());
    return runtime::RuntimeResult{};
}

runtime::RuntimeResult Il2CppResolver::find_method(const Il2CppMethodQuery& query, Il2CppMethodView* out) const {
    if (out != nullptr) {
        *out = Il2CppMethodView{};
    }
    if (query.klass == nullptr || query.name.empty() || query.arg_count < -1 || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp method query"};
    }
    if (query.params.size() > kMaxMethodParams) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "il2cpp method parameter count is too large"};
    }

    const std::string key = method_key(query);

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        const auto it = method_cache_.find(key);
        if (it != method_cache_.end()) {
            *out = it->second;
            ++cache_stats_.method_hits;
            NYX_LOGD("unity il2cpp resolver method cache hit");
            return runtime::RuntimeResult{};
        }
    }

    Il2CppApi api;
    const auto symbols = this->api(&api);
    if (!symbols.ok()) {
        return symbols;
    }

    if (api.class_get_methods != nullptr) {
        void* iter = nullptr;
        while (const MethodInfo* candidate = api.class_get_methods(query.klass, &iter)) {
            Il2CppMethodView view;
            const auto result = fill_method(api, candidate, &view);
            if (!result.ok()) {
                return result;
            }
            if (method_matches_query(query, view)) {
                *out = std::move(view);
                {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    method_cache_[key] = *out;
                }
                NYX_LOGD("unity il2cpp resolver found method: %s -> %s (%d params)",
                    out->name.c_str(), out->return_type.c_str(), out->arg_count);
                return runtime::RuntimeResult{};
            }
        }

        return not_found("il2cpp method signature not found");
    }

    const MethodInfo* method = api.class_get_method_from_name(query.klass, query.name.c_str(), query.arg_count);
    if (method == nullptr) {
        return not_found("il2cpp method not found");
    }

    const auto result = fill_method(api, method, out);
    if (!result.ok()) {
        return result;
    }
    if (!method_matches_query(query, *out)) {
        *out = Il2CppMethodView{};
        return not_found("il2cpp method signature not found");
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        method_cache_[key] = *out;
    }
    NYX_LOGD("unity il2cpp resolver found method: %s -> %s (%d params)",
        out->name.c_str(), out->return_type.c_str(), out->arg_count);
    return runtime::RuntimeResult{};
}

runtime::RuntimeResult Il2CppResolver::find_field(const Il2CppFieldQuery& query, Il2CppFieldView* out) const {
    if (out != nullptr) {
        *out = Il2CppFieldView{};
    }
    if (query.klass == nullptr || query.name.empty() || out == nullptr) {
        return runtime::RuntimeResult{runtime::RuntimeStatus::InvalidArgument, "missing il2cpp field query"};
    }

    const std::string key = field_key(query);

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        const auto it = field_cache_.find(key);
        if (it != field_cache_.end()) {
            *out = it->second;
            ++cache_stats_.field_hits;
            NYX_LOGD("unity il2cpp resolver field cache hit");
            return runtime::RuntimeResult{};
        }
    }

    Il2CppApi api;
    const auto symbols = this->api(&api);
    if (!symbols.ok()) {
        return symbols;
    }

    FieldInfo* field = api.class_get_field_from_name(query.klass, query.name.c_str());
    if (field == nullptr) {
        return not_found("il2cpp field not found");
    }

    const auto result = fill_field(api, field, out);
    if (!result.ok()) {
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        field_cache_[key] = *out;
    }
    NYX_LOGD("unity il2cpp resolver found field: %s : %s", out->name.c_str(), out->type_name.c_str());
    return runtime::RuntimeResult{};
}

Il2CppCacheStats Il2CppResolver::cache_stats() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_stats_;
}

void Il2CppResolver::clear_cache() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    api_cached_ = false;
    api_cache_ = Il2CppApi{};
    images_cached_ = false;
    image_cache_.clear();
    image_by_name_.clear();
    class_cache_.clear();
    method_cache_.clear();
    field_cache_.clear();
    cache_stats_ = Il2CppCacheStats{};
}

} // namespace unity
} // namespace engines
} // namespace nyx
