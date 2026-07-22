#pragma once

#include <string>

namespace nyx {
namespace engines {

enum class EngineKind {
    Unity,
    Unreal,
};

enum class EngineStatus {
    Available,
    Disabled,
    Unavailable,
    Failed,
};

struct EngineProbe {
    EngineKind kind = EngineKind::Unity;
    EngineStatus status = EngineStatus::Unavailable;
    std::string detail;

    bool available() const {
        return status == EngineStatus::Available;
    }
};

inline const char* status_name(EngineStatus status) {
    switch (status) {
        case EngineStatus::Available:
            return "available";
        case EngineStatus::Disabled:
            return "disabled";
        case EngineStatus::Unavailable:
            return "unavailable";
        case EngineStatus::Failed:
            return "failed";
    }

    return "unknown";
}

} // namespace engines
} // namespace nyx
