#pragma once

#include "src/core/auth/auth_context.h"
#include "src/core/auth/auth_session.h"

#include <string>

namespace nyx {
namespace core {
namespace auth {

// 保存当前会话到本地封存文件
bool save_session(const ContextState& context, const std::string& provider, const SessionState& session);
// 从本地封存文件恢复会话
bool load_session(const ContextState& context, const std::string& provider, const std::string& device_id, SessionState* session);
// 清除本地会话封存文件
void clear_session_store(const ContextState& context);
// 判断本地会话封存文件是否存在
bool has_session_store(const ContextState& context);
// 获取本地会话封存文件路径
std::string session_store_path(const ContextState& context);

} // namespace auth
} // namespace core
} // namespace nyx
