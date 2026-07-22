#include "sdk/include/hook.h"

#include "src/runtime/hook/hook_registry.h"
#include "src/runtime/hook/inline_backend.h"
#include "src/runtime/hook/plt_backend.h"
#include "src/runtime/loader/native_library.h"
#include "src/runtime/memory/memory_map.h"
#include "sdk/result_bridge.h"

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace nyx::sdk::hook {

namespace {

// Runtime hook 类型转 SDK hook 类型
Kind to_kind(runtime::hook::HookKind kind) {
    switch (kind) {
        case runtime::hook::HookKind::Inline:
            return Kind::Inline;
        case runtime::hook::HookKind::Plt:
            return Kind::Plt;
    }

    NYX_LOGW("unknown runtime HookKind: %d", static_cast<int>(kind));
    return Kind::Inline;
}

// Runtime hook 状态转 SDK hook 状态
State to_state(runtime::hook::HookState state) {
    switch (state) {
        case runtime::hook::HookState::Registered:
            return State::Registered;
        case runtime::hook::HookState::Installed:
            return State::Installed;
        case runtime::hook::HookState::Removed:
            return State::Removed;
        case runtime::hook::HookState::Failed:
            return State::Failed;
    }

    NYX_LOGW("unknown runtime HookState: %d", static_cast<int>(state));
    return State::Failed;
}

// SDK 使用独立 owner，避免同一 registry 中的其他调用方发生冲突。
runtime::hook::HookRegistry& registry() {
    static runtime::hook::HookRegistry value;
    return value;
}

// 安全复制 C 字符串
std::string text_or_empty(const char* text) {
    return text != nullptr ? text : "";
}

// 获取 PLT caller 路径
std::string caller_path(const PltOptions& options) {
    return text_or_empty(options.caller);
}

// 获取 PLT callee 路径
std::string callee_path(const PltOptions& options) {
    return text_or_empty(options.callee);
}

// key 仅用于 registry 去重，不是可对外持久化的 SDK 标识。
std::string key_for(const void* target) {
    return "inline:" + std::to_string(reinterpret_cast<std::uintptr_t>(target));
}

// 生成 PLT hook 去重键
std::string key_for(const PltOptions& request) {
    return "plt:" +
        caller_path(request) +
        ":" +
        callee_path(request) +
        ":" +
        text_or_empty(request.symbol);
}

// 构造无效参数结果
Result invalid_result(const char* detail) {
    return Result{Status::InvalidArgument, detail != nullptr ? detail : "invalid hook request"};
}

// 从内存映射中取模块最低起始地址作为基址
std::uintptr_t module_base(const std::vector<runtime::memory::MemoryMapEntry>& entries) {
    std::uintptr_t base = std::numeric_limits<std::uintptr_t>::max();
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.start == 0) {
            continue;
        }

        base = std::min(base, entry.start);
        found = true;
    }

    return found ? base : 0;
}

// 判断地址是否落在模块映射范围内
bool mapped_address(
    const std::vector<runtime::memory::MemoryMapEntry>& entries,
    std::uintptr_t address
) {
    return std::any_of(entries.begin(), entries.end(), [address](const auto& entry) {
        return entry.contains(address);
    });
}

// Runtime hook 记录转 SDK 快照
Record to_record(const runtime::hook::HookRecord& record) {
    Record out;
    out.kind = to_kind(record.kind);
    out.state = to_state(record.state);
    out.status = bridge::status_from(record.result.status);
    if (record.kind == runtime::hook::HookKind::Plt) {
        const auto stats = runtime::hook::plt_stats(record);
        out.hit_count = stats.hooked_count;
        out.failure_count = stats.failed_count;
        out.hooked_count = stats.hooked_count;
        out.unhooked_count = stats.failed_count;
    }
    out.target = record.target;
    out.symbol = record.symbol;
    out.detail = record.result.detail;
    return out;
}

// 登记并安装 hook，成功时回填 original
Result install_record(
    runtime::hook::HookRecord record,
    runtime::hook::HookBackend& backend,
    void** original,
    const char* conflict_detail,
    const char* log_prefix
) {
    const std::string key = record.target;
    if (!registry().add(std::move(record))) {
        return Result{Status::InvalidArgument, conflict_detail};
    }

    const auto result = registry().install("sdk", key, backend);
    if (!result.ok()) {
        NYX_LOGE("%s: %s", log_prefix, result.detail.c_str());
        return bridge::result_from(result);
    }

    if (original != nullptr) {
        const auto installed = registry().find("sdk", key);
        *original = installed.has_value() ? installed->original : nullptr;
    }
    return bridge::result_from(result);
}

// 获取 SDK owner 下的全部 hook 快照
std::vector<Record> sdk_records() {
    std::vector<Record> out;
    for (const auto& record : registry().records()) {
        if (record.owner == "sdk") {
            out.push_back(to_record(record));
        }
    }
    return out;
}

} // namespace

// 安装裸地址 inline hook
Result InlineRaw(void* target, void* replacement, void** original) {
    if (original != nullptr) {
        *original = nullptr;
    }
    if (target == nullptr || replacement == nullptr) {
        return Result{Status::InvalidArgument, "missing inline hook target or replacement"};
    }

    const std::string key = key_for(target);
    runtime::hook::HookRecord record;
    record.owner = "sdk";
    record.target = key;
    record.kind = runtime::hook::HookKind::Inline;
    record.target_address = target;
    record.replacement = replacement;

    return install_record(
        std::move(record),
        runtime::hook::inline_backend(),
        original,
        "inline hook target is already registered with another request",
        "sdk inline hook install failed"
    );
}

// 将已加载动态库中的相对偏移解析为绝对地址
Value<void*> ResolveOffset(const char* library, std::uintptr_t offset) {
    Value<void*> out;
    out.value = nullptr;
    if (library == nullptr || library[0] == '\0') {
        out.result = invalid_result("missing library name");
        return out;
    }

    runtime::memory::MemoryMap map;
    std::vector<runtime::memory::MemoryMapEntry> entries;
    const auto found = map.find_library(library, &entries);
    if (!found.ok()) {
        out.result = bridge::result_from(found);
        return out;
    }

    const std::uintptr_t base = module_base(entries);
    if (base == 0) {
        out.result = Result{Status::NotFound, "module base address was not found"};
        return out;
    }
    if (offset > std::numeric_limits<std::uintptr_t>::max() - base) {
        out.result = invalid_result("module offset overflows address space");
        return out;
    }

    const std::uintptr_t address = base + offset;
    if (!mapped_address(entries, address)) {
        out.result = Result{Status::NotFound, "module offset is not mapped"};
        return out;
    }

    out.value = reinterpret_cast<void*>(address);
    return out;
}

// 在已加载动态库中解析导出符号地址
Value<void*> ResolveSymbol(const char* library, const char* symbol) {
    Value<void*> out;
    out.value = nullptr;
    if (library == nullptr || library[0] == '\0' || symbol == nullptr || symbol[0] == '\0') {
        out.result = invalid_result("missing library or symbol name");
        return out;
    }

    runtime::loader::NativeLibrary loader;
    runtime::loader::LoadHandle handle;
    const auto load = loader.load(
        runtime::loader::LoadRequest{library, RTLD_NOW | RTLD_NOLOAD},
        &handle
    );
    if (!load.ok()) {
        out.result = bridge::result_from(load);
        return out;
    }

    runtime::loader::Symbol resolved;
    const auto find = loader.find_symbol(
        runtime::loader::SymbolRequest{handle.handle, symbol},
        &resolved
    );
    const auto close = loader.close(&handle);
    if (!find.ok()) {
        out.result = bridge::result_from(find);
        return out;
    }
    if (!close.ok()) {
        out.result = bridge::result_from(close);
        return out;
    }

    out.value = resolved.address;
    return out;
}

// 按库偏移安装 inline hook
Result InlineOffsetRaw(const char* library, std::uintptr_t offset, void* replacement, void** original) {
    if (original != nullptr) {
        *original = nullptr;
    }
    if (replacement == nullptr) {
        return invalid_result("missing inline hook replacement");
    }

    const auto target = ResolveOffset(library, offset);
    if (!target.ok()) {
        return target.result;
    }

    return InlineRaw(target.value, replacement, original);
}

// 按导出符号安装 inline hook
Result InlineSymbolRaw(const char* library, const char* symbol, void* replacement, void** original) {
    if (original != nullptr) {
        *original = nullptr;
    }
    if (replacement == nullptr) {
        return invalid_result("missing inline hook replacement");
    }

    const auto target = ResolveSymbol(library, symbol);
    if (!target.ok()) {
        return target.result;
    }

    return InlineRaw(target.value, replacement, original);
}

// 按库偏移卸载 inline hook
Result UnhookInlineOffset(const char* library, std::uintptr_t offset) {
    const auto target = ResolveOffset(library, offset);
    if (!target.ok()) {
        return target.result;
    }

    return UnhookInline(target.value);
}

// 按导出符号卸载 inline hook
Result UnhookInlineSymbol(const char* library, const char* symbol) {
    const auto target = ResolveSymbol(library, symbol);
    if (!target.ok()) {
        return target.result;
    }

    return UnhookInline(target.value);
}

// 按目标地址卸载 inline hook
Result UnhookInline(void* target) {
    if (target == nullptr) {
        return Result{Status::InvalidArgument, "missing inline hook target"};
    }

    const auto result = registry().remove("sdk", key_for(target), runtime::hook::inline_backend());
    return bridge::result_from(result);
}

// 按 PLT 请求安装 hook
Result PltRaw(const PltOptions& request, void** original) {
    if (original != nullptr) {
        *original = nullptr;
    }
    if (request.symbol == nullptr || request.symbol[0] == '\0' || request.replacement == nullptr) {
        return Result{Status::InvalidArgument, "missing PLT hook symbol or replacement"};
    }

    const std::string key = key_for(request);
    runtime::hook::HookRecord record;
    record.owner = "sdk";
    record.target = key;
    record.kind = runtime::hook::HookKind::Plt;
    record.caller_path = caller_path(request);
    record.callee_path = callee_path(request);
    record.symbol = text_or_empty(request.symbol);
    record.replacement = request.replacement;

    return install_record(
        std::move(record),
        runtime::hook::plt_backend(),
        original,
        "PLT hook target is already registered with another request",
        "sdk PLT hook install failed"
    );
}

// 按 PLT 请求卸载 hook
Result UnhookPlt(const PltOptions& request) {
    if (request.symbol == nullptr || request.symbol[0] == '\0') {
        return Result{Status::InvalidArgument, "missing PLT hook symbol"};
    }

    const auto result = registry().remove("sdk", key_for(request), runtime::hook::plt_backend());
    return bridge::result_from(result);
}

// 获取 hook 快照，out 为空时只回填所需数量
Result Get(Record* out, std::size_t* count) {
    if (count == nullptr) {
        return Result{Status::InvalidArgument, "missing hook record count"};
    }

    const auto records = sdk_records();

    const std::size_t needed = records.size();
    if (out == nullptr) {
        *count = needed;
        return Result{};
    }
    if (*count < needed) {
        *count = needed;
        return Result{Status::InvalidArgument, "hook record buffer is too small"};
    }

    for (std::size_t i = 0; i < needed; ++i) {
        out[i] = records[i];
    }
    *count = needed;
    return Result{};
}

// 获取 hook 快照 vector
Value<std::vector<Record>> Get() {
    Value<std::vector<Record>> out;
    out.value = sdk_records();
    return out;
}

// 保存 replacement 返回地址，用于析构时平衡 ByteHook 调用栈
PltCallScope::PltCallScope(void* return_address) : return_address_(return_address) {}

// 离开 replacement 时平衡 ByteHook 调用栈
PltCallScope::~PltCallScope() {
    runtime::hook::plt_pop_stack(return_address_);
}

} // namespace nyx::sdk::hook
