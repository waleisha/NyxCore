#include "src/runtime/hook/plt_backend.h"

#include "sdk/include/utils.h"

#ifndef NYX_PLT_HOOK_HAS_BYTEHOOK
#error "NYX_PLT_HOOK_HAS_BYTEHOOK must be defined by CMake"
#endif

#ifndef NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER
#error "NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER must be defined by CMake"
#endif

#if NYX_PLT_HOOK_HAS_BYTEHOOK
#include "bytehook.h"
#include <memory>
#include <mutex>
#include <string>
#if NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER
#include "shadowhook.h"
#endif
#endif

namespace nyx::runtime::hook {

namespace {

#if NYX_PLT_HOOK_HAS_BYTEHOOK

// ByteHook 同步安装回调写入的状态
struct ByteHookHandle {
    // ByteHook stub，用于卸载
    bytehook_stub_t stub = nullptr;
    // 单点 hook 的最终状态
    int status = BYTEHOOK_STATUS_CODE_MAX;
    // hook_all 时记录的第一个失败状态
    int first_failure = BYTEHOOK_STATUS_CODE_MAX;
    // 成功 hook 的位置数量
    int hooked_count = 0;
    // 失败的位置数量
    int failed_count = 0;
    // ByteHook 返回的原函数地址
    void* previous = nullptr;
    // 是否为 hook_single 请求
    bool single = true;
};

// 初始化底层 hook 后端
RuntimeResult init_backend_once() {
#if NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER
    const int shadowhook_result = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (shadowhook_result != SHADOWHOOK_ERRNO_OK) {
        std::string detail = "shadowhook_init failed: ";
        detail += std::to_string(shadowhook_result);
        return RuntimeResult{RuntimeStatus::Failed, detail};
    }
#endif

    const int bytehook_result = bytehook_init(BYTEHOOK_MODE_AUTOMATIC, false);
    if (bytehook_result != BYTEHOOK_STATUS_CODE_OK) {
        std::string detail = "bytehook_init failed: ";
        detail += std::to_string(bytehook_result);
        return RuntimeResult{RuntimeStatus::Failed, detail};
    }

    return RuntimeResult{};
}

// 只初始化一次底层 hook 后端
RuntimeResult init_backend() {
    static std::once_flag flag;
    static RuntimeResult result;
    std::call_once(flag, []() {
        result = init_backend_once();
    });
    return result;
}

// ByteHook 安装回调，统计成功和失败位置
void on_hooked(
    bytehook_stub_t stub,
    int status,
    const char*,
    const char*,
    void*,
    void* previous,
    void* arg
) {
    auto* handle = static_cast<ByteHookHandle*>(arg);
    if (handle == nullptr) {
        return;
    }

    handle->stub = stub;
    handle->status = status;
    if (status == BYTEHOOK_STATUS_CODE_OK) {
        ++handle->hooked_count;
        if (handle->previous == nullptr) {
            handle->previous = previous;
        }
    } else {
        ++handle->failed_count;
        if (handle->first_failure == BYTEHOOK_STATUS_CODE_MAX) {
            handle->first_failure = status;
        }
    }
}

// 格式化 ByteHook 错误详情
std::string hook_detail(const char* prefix, int status) {
    std::string detail = prefix != nullptr ? prefix : "bytehook failed";
    detail += ": ";
    detail += std::to_string(status);
    return detail;
}

// 根据 ByteHook 回调统计结果生成安装状态
RuntimeResult install_status(const ByteHookHandle& handle) {
    if (!handle.single) {
        if (handle.hooked_count == 0 && handle.failed_count > 0) {
            return RuntimeResult{
                RuntimeStatus::Failed,
                hook_detail("bytehook hook failed", handle.first_failure)
            };
        }
        if (handle.failed_count > 0) {
            NYX_LOGW(
                "bytehook partial failure: %d/%d",
                handle.failed_count,
                handle.hooked_count + handle.failed_count
            );
        }

        return RuntimeResult{};
    }

    if (handle.status == BYTEHOOK_STATUS_CODE_OK) {
        return RuntimeResult{};
    }
    if (handle.status == BYTEHOOK_STATUS_CODE_NOSYM || handle.status == BYTEHOOK_STATUS_CODE_MAX) {
        return RuntimeResult{
            RuntimeStatus::NotFound,
            hook_detail("bytehook symbol was not hooked", handle.status)
        };
    }

    return RuntimeResult{RuntimeStatus::Failed, hook_detail("bytehook hook failed", handle.status)};
}

// 安装 PLT hook，并保存 ByteHook handle 供卸载使用
RuntimeResult install_plt(HookRecord& record) {
    auto init = init_backend();
    if (!init.ok()) {
        return init;
    }

    auto handle = std::make_shared<ByteHookHandle>();
    handle->single = !record.caller_path.empty();
    bytehook_stub_t stub = handle->single
        ? bytehook_hook_single(
            record.caller_path.c_str(),
            record.callee_path.empty() ? nullptr : record.callee_path.c_str(),
            record.symbol.c_str(),
            record.replacement,
            on_hooked,
            handle.get()
        )
        : bytehook_hook_all(
            record.callee_path.empty() ? nullptr : record.callee_path.c_str(),
            record.symbol.c_str(),
            record.replacement,
            on_hooked,
            handle.get()
        );
    if (stub == nullptr) {
        return RuntimeResult{RuntimeStatus::Failed, "bytehook hook task failed"};
    }
    handle->stub = stub;

    // ByteHook 会为本次安装同步触发 on_hooked
    auto status = install_status(*handle);
    if (!status.ok()) {
        bytehook_unhook(stub);
        return status;
    }

    record.original = handle->previous;
    record.backend_data = handle;
    return RuntimeResult{};
}

// 卸载 PLT hook
RuntimeResult remove_plt(HookRecord& record) {
    if (record.state != HookState::Installed) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "PLT hook is not installed"};
    }
    if (record.backend_data == nullptr) {
        return RuntimeResult{RuntimeStatus::InvalidArgument, "missing PLT hook handle"};
    }

    auto handle = std::static_pointer_cast<ByteHookHandle>(record.backend_data);
    const int result = bytehook_unhook(handle->stub);
    if (result != BYTEHOOK_STATUS_CODE_OK) {
        return RuntimeResult{RuntimeStatus::Failed, hook_detail("bytehook_unhook failed", result)};
    }

    record.backend_data.reset();
    record.original = nullptr;
    return RuntimeResult{};
}

#endif

// ByteHook PLT hook 后端
class PltHookBackend final : public HookBackend {
public:
    // 安装 PLT hook
    RuntimeResult install(HookRecord& record) override {
        if (!plt_hook_available()) {
            return RuntimeResult{RuntimeStatus::Unavailable, "PLT hook backend is unavailable for this ABI"};
        }
        if (record.kind != HookKind::Plt || record.symbol.empty() || record.replacement == nullptr) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "missing PLT hook symbol or replacement"};
        }

#if NYX_PLT_HOOK_HAS_BYTEHOOK
        return install_plt(record);
#else
        return RuntimeResult{RuntimeStatus::Unavailable, "PLT hook backend is unavailable for this ABI"};
#endif
    }

    // 移除 PLT hook
    RuntimeResult remove(HookRecord& record) override {
        if (!plt_hook_available()) {
            return RuntimeResult{RuntimeStatus::Unavailable, "PLT hook backend is unavailable for this ABI"};
        }

#if NYX_PLT_HOOK_HAS_BYTEHOOK
        return remove_plt(record);
#else
        return RuntimeResult{RuntimeStatus::Unavailable, "PLT hook backend is unavailable for this ABI"};
#endif
    }
};

} // namespace

// 判断当前 ABI 是否编译并初始化了 PLT 后端
bool plt_hook_available() {
#if NYX_PLT_HOOK_HAS_BYTEHOOK
    return init_backend().ok();
#else
    return false;
#endif
}

// 获取 hook 记录里的 PLT 安装统计
PltHookStats plt_stats(const HookRecord& record) {
    PltHookStats stats;
#if NYX_PLT_HOOK_HAS_BYTEHOOK
    if (record.kind == HookKind::Plt && record.backend_data != nullptr) {
        auto handle = std::static_pointer_cast<ByteHookHandle>(record.backend_data);
        stats.hooked_count = static_cast<std::size_t>(handle->hooked_count);
        stats.failed_count = static_cast<std::size_t>(handle->failed_count);
    }
#endif
    return stats;
}

// 在 replacement 内平衡 ByteHook automatic mode 的调用栈
void plt_pop_stack(void* return_address) {
#if NYX_PLT_HOOK_HAS_BYTEHOOK
    if (BYTEHOOK_MODE_AUTOMATIC == bytehook_get_mode()) {
        bytehook_pop_stack(return_address);
    }
#else
    static_cast<void>(return_address);
#endif
}

// 获取进程级 ByteHook PLT hook 后端
HookBackend& plt_backend() {
    static PltHookBackend backend;
    return backend;
}

} // namespace nyx::runtime::hook
