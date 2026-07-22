#pragma once

#include "src/runtime/hook/hook_backend.h"

namespace nyx::runtime::hook {

// 获取进程级 Dobby inline hook 后端
HookBackend& inline_backend();

} // namespace nyx::runtime::hook
