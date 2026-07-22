#pragma once

#include <cstddef>
#include <memory>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {

// 模块接口：宿主按生命周期回调插件逻辑
class NYX_EXPORT IMod {
public:
    virtual ~IMod() = default;

    // 模块初始化时调用一次
    virtual void OnInit() = 0;
    // 每帧更新时调用
    virtual void OnUpdate() = 0;
    // 每帧绘制 UI 时调用
    virtual void OnDraw() = 0;
};

// 模块工厂：返回一个新的模块实例
using Factory = std::unique_ptr<IMod> (*)();

// 模块注册信息
struct Info {
    // 模块名称
    const char* name = nullptr;
    // 关联的授权功能名
    const char* feature = nullptr;
    // 未指定功能开关时是否默认启用
    bool enabled_by_default = true;
};

// 按名称注册模块
NYX_EXPORT bool Register(const char* name, Factory factory);
// 按完整信息注册模块
NYX_EXPORT bool Register(const Info& info, Factory factory);

} // namespace sdk
} // namespace nyx

// 模块入口：动态库加载后由宿主调用
extern "C" NYX_EXPORT void ModEntry();
