#include "src/runtime/loader/native_library.h"

#include <dlfcn.h>

namespace nyx {
namespace runtime {
namespace loader {

RuntimeResult NativeLibrary::load(const LoadRequest& request, LoadHandle* out) const {
    if (request.path.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing library path or output handle"};
    }

    const int flags = request.flags != 0 ? request.flags : RTLD_NOW;
    dlerror();
    void* handle = dlopen(request.path.c_str(), flags);
    if (handle == nullptr) {
        const char* error = dlerror();
        return RuntimeResult{RuntimeStatus::NotFound, error != nullptr ? error : "dlopen failed"};
    }

    out->handle = handle;
    out->path = request.path;
    return RuntimeResult{};
}

RuntimeResult NativeLibrary::close(LoadHandle* handle) const {
    if (handle == nullptr || handle->handle == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing native library handle"};
    }

    if (dlclose(handle->handle) != 0) {
        const char* error = dlerror();
        return RuntimeResult{RuntimeStatus::Failed, error != nullptr ? error : "dlclose failed"};
    }

    handle->handle = nullptr;
    handle->path.clear();
    return RuntimeResult{};
}

RuntimeResult NativeLibrary::find_symbol(const SymbolRequest& request, Symbol* out) const {
    if (request.handle == nullptr || request.name.empty() || out == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing handle, symbol name, or output"};
    }

    dlerror();
    void* address = dlsym(request.handle, request.name.c_str());
    const char* error = dlerror();
    if (error != nullptr || address == nullptr) {
        return RuntimeResult{RuntimeStatus::NotFound, error != nullptr ? error : "symbol not found"};
    }

    out->address = address;
    out->name = request.name;
    return RuntimeResult{};
}

} // namespace loader
} // namespace runtime
} // namespace nyx
