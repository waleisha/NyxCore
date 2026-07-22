#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/engines/base/engine_status.h"
#include "src/engines/unity/il2cpp_runtime.h"
#include "src/runtime/runtime_result.h"

namespace nyx {
namespace engines {
namespace unity {

struct Il2CppImageView {
    const Il2CppImage* image = nullptr;
    std::string name;

    bool valid() const {
        return image != nullptr;
    }
};

struct Il2CppClassView {
    Il2CppClass* klass = nullptr;
    std::string class_namespace;
    std::string name;

    bool valid() const {
        return klass != nullptr;
    }
};

struct Il2CppParamView {
    std::string name;
    std::string type_name;
};

struct Il2CppMethodView {
    const MethodInfo* method = nullptr;
    std::string name;
    int arg_count = -1;
    std::string return_type;
    std::vector<Il2CppParamView> params;

    bool valid() const {
        return method != nullptr;
    }
};

struct Il2CppFieldView {
    FieldInfo* field = nullptr;
    std::string name;
    std::string type_name;

    bool valid() const {
        return field != nullptr;
    }
};

struct Il2CppCacheStats {
    std::size_t image_hits = 0;
    std::size_t class_hits = 0;
    std::size_t method_hits = 0;
    std::size_t field_hits = 0;
};

struct Il2CppImageQuery {
    std::string image_name;
};

struct Il2CppClassQuery {
    const Il2CppImage* image = nullptr;
    std::string class_namespace;
    std::string name;
};

struct Il2CppMethodQuery {
    Il2CppClass* klass = nullptr;
    std::string name;
    int arg_count = -1;
    std::vector<std::string> params;
};

struct Il2CppFieldQuery {
    Il2CppClass* klass = nullptr;
    std::string name;
};

class Il2CppResolver {
public:
    Il2CppResolver() = default;
    explicit Il2CppResolver(const Il2CppApi& api);

    EngineProbe probe() const;
    runtime::RuntimeResult images(std::vector<Il2CppImageView>* out) const;
    runtime::RuntimeResult find_image(const Il2CppImageQuery& query, Il2CppImageView* out) const;
    runtime::RuntimeResult find_class(const Il2CppClassQuery& query, Il2CppClassView* out) const;
    runtime::RuntimeResult find_method(const Il2CppMethodQuery& query, Il2CppMethodView* out) const;
    runtime::RuntimeResult find_field(const Il2CppFieldQuery& query, Il2CppFieldView* out) const;
    Il2CppCacheStats cache_stats() const;
    void clear_cache() const;

private:
    runtime::RuntimeResult api(Il2CppApi* out) const;

    Il2CppRuntime runtime_;
    mutable std::mutex cache_mutex_;
    mutable bool api_cached_ = false;
    mutable Il2CppApi api_cache_;
    mutable bool images_cached_ = false;
    mutable std::vector<Il2CppImageView> image_cache_;
    mutable std::unordered_map<std::string, Il2CppImageView> image_by_name_;
    mutable std::unordered_map<std::string, Il2CppClassView> class_cache_;
    mutable std::unordered_map<std::string, Il2CppMethodView> method_cache_;
    mutable std::unordered_map<std::string, Il2CppFieldView> field_cache_;
    mutable Il2CppCacheStats cache_stats_;
};

} // namespace unity
} // namespace engines
} // namespace nyx
