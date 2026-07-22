#pragma once

#include <string>

#include "sdk/include/mod.h"

namespace nyx {
namespace core {

// 模块注册信息：由 SDK 注册入口转换后交给控制器保存
struct ModuleSpec {
    // 模块名称，作为唯一注册键
    std::string name;
    // 鉴权功能标识，空字符串表示无需鉴权
    std::string feature;
    // 模块实例工厂
    sdk::Factory factory = nullptr;
    // 注册后是否默认启用
    bool enabled_by_default = true;
};

} // namespace core
} // namespace nyx
