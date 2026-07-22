#pragma once

#include "sdk/include/auth.h"

#include <memory>
#include <string>

namespace nyx {
namespace core {
namespace auth {
namespace doctor {

// 诊断快照，用于测试工具保存鉴权内部状态
struct Snapshot {
    // 不透明状态指针
    std::shared_ptr<void> state;
};

// 保存当前鉴权状态
Snapshot Save();
// 恢复鉴权状态
void Restore(const Snapshot& snapshot);
// 重置鉴权状态
void Reset();
// 切换到无 provider 模式
void UseNoProvider();
// 切换到 mock provider
void UseMock();
// 设置 mock 心跳失败次数
void SetHeartbeatFailures(int count);
// 触发一次心跳
sdk::auth::Result Heartbeat();
// 设置当前线程的渲染线程标记
void SetRenderThread(bool enabled);
// 获取当前设备 ID
std::string DeviceId();
// 判断本地会话封存文件是否存在
bool IsLoggedInStore();
// 获取本地会话封存文件路径
std::string SessionStorePath();
// 运行 WY provider 诊断
bool RunWyDoctor();

} // namespace doctor
} // namespace auth
} // namespace core
} // namespace nyx
