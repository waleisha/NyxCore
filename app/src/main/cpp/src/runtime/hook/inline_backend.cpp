#include "src/runtime/hook/inline_backend.h"

#include "dobby.h"

#include <string>

namespace nyx::runtime::hook {

namespace {

// 转换 Dobby 错误码
RuntimeResult dobby_result(const char* operation, int code) {
    std::string detail = operation;
    detail += " failed: ";
    detail += std::to_string(code);
    return RuntimeResult{RuntimeStatus::Failed, detail};
}

// Dobby inline hook 后端，负责安装和销毁 trampoline
class InlineHookBackend final : public HookBackend {
public:
    // 安装 inline hook，并回填 original trampoline
    RuntimeResult install(HookRecord& record) override {
        if (record.kind != HookKind::Inline || record.target_address == nullptr || record.replacement == nullptr) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "missing inline hook target or replacement"};
        }

        void* original = nullptr;
        const int result = DobbyHook(record.target_address, record.replacement, &original);
        if (result != RT_SUCCESS) {
            return dobby_result("DobbyHook", result);
        }

        record.original = original;
        return RuntimeResult{};
    }

    // 移除 inline hook
    RuntimeResult remove(HookRecord& record) override {
        if (record.state != HookState::Installed) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "inline hook is not installed"};
        }
        if (record.target_address == nullptr) {
            return RuntimeResult{RuntimeStatus::InvalidArgument, "missing inline hook target"};
        }

        const int result = DobbyDestroy(record.target_address);
        if (result != RT_SUCCESS) {
            return dobby_result("DobbyDestroy", result);
        }

        record.original = nullptr;
        return RuntimeResult{};
    }
};

} // namespace

// 获取进程级 Dobby inline hook 后端
HookBackend& inline_backend() {
    static InlineHookBackend backend;
    return backend;
}

} // namespace nyx::runtime::hook
