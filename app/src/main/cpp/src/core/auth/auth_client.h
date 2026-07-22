#pragma once

#include "sdk/include/auth.h"

#include <cstddef>
#include <memory>
#include <string>

namespace nyx {
namespace core {
namespace auth {

namespace client {

// 鉴权客户端快照，用于测试或诊断时保存内部状态
struct Snapshot {
    // 不透明状态指针
    std::shared_ptr<void> state;
};

// 配置鉴权上下文和 provider
sdk::auth::Result configure(const sdk::auth::Context& context, const sdk::auth::InitConfig& config);
// 登录并建立会话
sdk::auth::Result login(const char* license);
// 登出并清理本地会话
void logout();
// 判断当前是否有有效会话
bool has_session();
// 判断当前会话是否授权指定功能
bool is_feature_licensed(const std::string& feature);
// 导出短生命周期能力票据
sdk::auth::Value<sdk::auth::Capability> export_capability(sdk::auth::CapabilityPurpose purpose);
// 校验短生命周期能力票据
bool verify_capability(sdk::auth::CapabilityPurpose purpose, const sdk::auth::Capability& capability);
// 判断鉴权客户端是否已准备好
bool is_ready();
// 获取远程变量
sdk::auth::Result fetch_var(const char* key, char* out, std::size_t out_len);
// 获取公告
sdk::auth::Result fetch_notice(char* out, std::size_t out_len);
// 检查更新
sdk::auth::Result fetch_update(char* out, std::size_t out_len);
// 执行心跳
sdk::auth::Result heartbeat();

// 保存当前客户端状态快照
Snapshot save_snapshot();
// 恢复客户端状态快照
void restore_snapshot(const Snapshot& snapshot);
// 重置客户端状态
void reset();
// 切换到无 provider 模式
void use_no_provider();
// 切换到 mock provider，仅测试构建可用
void use_mock();
// 设置 mock 心跳失败次数
void set_heartbeat_failures(int count);
// 设置当前线程的渲染线程标记
void set_render_thread(bool enabled);
// 获取当前设备 ID
std::string device_id();
// 判断本地会话封存文件是否存在
bool has_session_store();
// 获取本地会话封存文件路径
std::string session_store_path();

} // namespace client

// 判断当前会话是否授权指定功能
bool is_feature_licensed(const std::string& feature);
// 判断鉴权客户端是否已准备好
bool is_ready();

} // namespace auth
} // namespace core
} // namespace nyx
