#pragma once

namespace nyx {
namespace app_bridge {

// 初始化 native runtime 和已注册模块。
void init_runtime();

// 判断 native runtime 是否仍处于运行态。
bool is_running();

// 标记 native runtime 关闭状态。
void set_shutdown(bool shutdown);

} // namespace app_bridge
} // namespace nyx
