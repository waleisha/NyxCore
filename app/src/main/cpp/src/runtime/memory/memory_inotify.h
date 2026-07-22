#pragma once

#include "src/runtime/runtime_result.h"

namespace nyx {
namespace runtime {
namespace memory {

// inotify watcher 管理
class InotifyManager {
public:
    // 清理本进程 watcher，并报告外部 watcher 数量
    static RuntimeResult clear_watchers();
};

} // namespace memory
} // namespace runtime
} // namespace nyx
