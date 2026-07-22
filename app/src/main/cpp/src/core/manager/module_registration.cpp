#include "src/core/manager/module_controller.h"

#include <utility>

namespace nyx {
namespace sdk {

// 使用名称和工厂注册模块
bool Register(const char* name, Factory factory) {
    Info info;
    info.name = name;
    return Register(info, factory);
}

// 将 SDK 注册信息转换为核心模块规格
bool Register(const Info& info, Factory factory) {
    core::ModuleSpec spec;
    if (info.name != nullptr) {
        spec.name = info.name;
    }
    if (info.feature != nullptr) {
        spec.feature = info.feature;
    }
    spec.factory = factory;
    spec.enabled_by_default = info.enabled_by_default;
    return core::ModuleController::Instance().Register(std::move(spec));
}

} // namespace sdk
} // namespace nyx
