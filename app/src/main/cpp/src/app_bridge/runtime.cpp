#include "src/app_bridge/runtime.h"

#include "sdk/include/mod.h"
#include "sdk/include/utils.h"
#include "src/core/manager/module_controller.h"

#include <mutex>

namespace nyx {
namespace app_bridge {
namespace {

// native runtime 的进程级状态。
struct RuntimeState {
    // surface 或 Kotlin 显式关闭后停止绘制。
    bool shutdown = false;
};

std::mutex g_runtime_mutex;
RuntimeState g_runtime_state;
std::once_flag g_init_once;

} // namespace

// 只初始化一次模块入口和模块控制器。
void init_runtime() {
    std::call_once(g_init_once, [] {
        ModEntry();

        auto& controller = core::ModuleController::Instance();
        NYX_LOGI("NyxCore native runtime initialized");
        controller.PreInitAll();
        controller.InitializeAll();
    });
}

// 绘制循环用它判断 runtime 是否还能继续执行。
bool is_running() {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    return !g_runtime_state.shutdown;
}

// 更新 native runtime 的关闭标记。
void set_shutdown(bool shutdown) {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    g_runtime_state.shutdown = shutdown;
}

} // namespace app_bridge
} // namespace nyx
