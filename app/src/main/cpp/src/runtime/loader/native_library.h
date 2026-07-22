#pragma once

#include <string>

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace loader {

struct LoadRequest {
    std::string path;
    int flags = 0;
};

struct LoadHandle {
    void* handle = nullptr;
    std::string path;

    bool valid() const {
        return handle != nullptr;
    }
};

struct SymbolRequest {
    void* handle = nullptr;
    std::string name;
};

struct Symbol {
    void* address = nullptr;
    std::string name;

    bool found() const {
        return address != nullptr;
    }
};

class NativeLibrary {
public:
    RuntimeResult load(const LoadRequest& request, LoadHandle* out) const;
    RuntimeResult close(LoadHandle* handle) const;
    RuntimeResult find_symbol(const SymbolRequest& request, Symbol* out) const;
};

} // namespace loader
} // namespace runtime
} // namespace nyx
