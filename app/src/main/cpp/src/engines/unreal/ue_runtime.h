#pragma once

#include "src/engines/base/engine_status.h"

namespace nyx {
namespace engines {
namespace unreal {

class UeRuntime {
public:
    EngineProbe probe() const;
};

} // namespace unreal
} // namespace engines
} // namespace nyx
