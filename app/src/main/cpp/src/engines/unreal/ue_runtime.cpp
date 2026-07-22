#include "src/engines/unreal/ue_runtime.h"

#include <dlfcn.h>

namespace nyx {
namespace engines {
namespace unreal {

EngineProbe UeRuntime::probe() const {
    EngineProbe probe;
    probe.kind = EngineKind::Unreal;

    void* handle = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) {
        probe.status = EngineStatus::Unavailable;
        probe.detail = "libUE4.so is not loaded";
        return probe;
    }

    dlclose(handle);
    probe.status = EngineStatus::Available;
    probe.detail = "libUE4.so is loaded";
    return probe;
}

} // namespace unreal
} // namespace engines
} // namespace nyx
