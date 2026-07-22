#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx::sdk::hook {

// Hook 的实现类型
enum class Kind { Inline, Plt };

// Hook 从登记到卸载的生命周期状态
enum class State { Registered, Installed, Removed, Failed };

// PLT hook 请求：caller/callee 为空时不限制对应动态库
struct PltOptions {
    // 目标符号名
    const char* symbol = nullptr;
    // 调用方动态库
    const char* caller = nullptr;
    // 被调用方动态库
    const char* callee = nullptr;
    // 替换函数
    void* replacement = nullptr;
};

// 面向 SDK 的 hook 状态快照，不包含 runtime 内部后端数据
struct Record {
    // Hook 类型
    Kind kind = Kind::Inline;
    // 生命周期状态
    State state = State::Registered;
    // 最近一次状态码
    Status status = Status::Ok;
    // 命中次数
    std::size_t hit_count = 0;
    // 失败次数
    std::size_t failure_count = 0;
    // 已 hook 数量
    std::size_t hooked_count = 0;
    // 已废弃：为源码兼容保留，值等于 failure_count
    std::size_t unhooked_count = 0;
    // 目标标识
    std::string target;
    // 符号名
    std::string symbol;
    // 状态详情
    std::string detail;
};

// 安装裸地址 inline hook，成功时向 original 回填原函数跳板
NYX_EXPORT Result InlineRaw(void* target, void* replacement, void** original = nullptr);
// 按目标地址卸载已安装的 inline hook
NYX_EXPORT Result UnhookInline(void* target);
// 将已加载动态库中的相对偏移解析为可 hook 的绝对地址
NYX_EXPORT Value<void*> ResolveOffset(const char* library, std::uintptr_t offset);
// 在已加载动态库中解析导出符号地址，不会加载新库
NYX_EXPORT Value<void*> ResolveSymbol(const char* library, const char* symbol);
// 按库偏移安装 inline hook
NYX_EXPORT Result InlineOffsetRaw(
    const char* library,
    std::uintptr_t offset,
    void* replacement,
    void** original = nullptr
);
// 按导出符号安装 inline hook
NYX_EXPORT Result InlineSymbolRaw(
    const char* library,
    const char* symbol,
    void* replacement,
    void** original = nullptr
);
// 按库偏移卸载 inline hook
NYX_EXPORT Result UnhookInlineOffset(const char* library, std::uintptr_t offset);
// 按导出符号卸载 inline hook
NYX_EXPORT Result UnhookInlineSymbol(const char* library, const char* symbol);
// 按 PLT 请求安装 hook，replacement 必须非空
NYX_EXPORT Result PltRaw(const PltOptions& options, void** original = nullptr);
// 按 PLT 请求卸载 hook
NYX_EXPORT Result UnhookPlt(const PltOptions& options);
// 获取 hook 快照，out 为空时只回填所需元素数量
NYX_EXPORT Result Get(Record* out, std::size_t* count);
// 获取 hook 快照 vector
NYX_EXPORT Value<std::vector<Record>> Get();

namespace detail {

// 限制模板参数必须是函数指针
template <class Fn>
struct IsFunctionPointer
    : std::bool_constant<std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>> {};

} // namespace detail

// 类型安全的 InlineRaw 封装
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result Inline(Fn target, Fn replacement, Fn* original = nullptr) {
    void* raw_original = nullptr;
    if (original != nullptr) *original = nullptr;
    const auto result = InlineRaw(
        reinterpret_cast<void*>(target), reinterpret_cast<void*>(replacement), original != nullptr ? &raw_original : nullptr
    );
    if (result.ok() && original != nullptr) *original = reinterpret_cast<Fn>(raw_original);
    return result;
}

// 对 void* 目标地址安装类型安全的 inline hook
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result Inline(void* target, Fn replacement, Fn* original = nullptr) {
    void* raw_original = nullptr;
    if (original != nullptr) *original = nullptr;
    const auto result = InlineRaw(target, reinterpret_cast<void*>(replacement), original != nullptr ? &raw_original : nullptr);
    if (result.ok() && original != nullptr) *original = reinterpret_cast<Fn>(raw_original);
    return result;
}

// 类型安全地卸载 inline hook
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result UnhookInline(Fn target) { return UnhookInline(reinterpret_cast<void*>(target)); }

// 按库偏移安装类型安全的 inline hook
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result InlineOffset(const char* library, std::uintptr_t offset, Fn replacement, Fn* original = nullptr) {
    void* raw_original = nullptr;
    if (original != nullptr) *original = nullptr;
    const auto result = InlineOffsetRaw(library, offset, reinterpret_cast<void*>(replacement), original != nullptr ? &raw_original : nullptr);
    if (result.ok() && original != nullptr) *original = reinterpret_cast<Fn>(raw_original);
    return result;
}

// 按导出符号安装类型安全的 inline hook
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result InlineSymbol(const char* library, const char* symbol, Fn replacement, Fn* original = nullptr) {
    void* raw_original = nullptr;
    if (original != nullptr) *original = nullptr;
    const auto result = InlineSymbolRaw(library, symbol, reinterpret_cast<void*>(replacement), original != nullptr ? &raw_original : nullptr);
    if (result.ok() && original != nullptr) *original = reinterpret_cast<Fn>(raw_original);
    return result;
}

// 自动平衡 ByteHook automatic mode 调用栈的作用域对象
class PltCallScope {
public:
    // 保存 replacement 返回地址
    explicit PltCallScope(void* return_address);
    // 离开 replacement 时平衡 ByteHook 调用栈
    ~PltCallScope();
    PltCallScope(const PltCallScope&) = delete;
    PltCallScope& operator=(const PltCallScope&) = delete;
private:
    // replacement 返回地址
    void* return_address_;
};

// 按 PLT 请求安装 hook
inline Result Plt(const PltOptions& options, void** original = nullptr) { return PltRaw(options, original); }

// 按符号安装类型安全的 PLT hook
template <class Fn, std::enable_if_t<detail::IsFunctionPointer<Fn>::value, int> = 0>
Result Plt(const char* symbol, Fn replacement, Fn* original = nullptr, const char* caller = nullptr) {
    void* raw_original = nullptr;
    if (original != nullptr) *original = nullptr;
    const auto result = PltRaw(
        PltOptions{symbol, caller, nullptr, reinterpret_cast<void*>(replacement)},
        original != nullptr ? &raw_original : nullptr
    );
    if (result.ok() && original != nullptr) *original = reinterpret_cast<Fn>(raw_original);
    return result;
}

// 按符号卸载 PLT hook
inline Result UnhookPlt(const char* symbol, const char* caller = nullptr) {
    return UnhookPlt(PltOptions{symbol, caller, nullptr, nullptr});
}

} // namespace nyx::sdk::hook

// 在每个 PLT replacement 的调用路径中创建一次，用于恢复 ByteHook 调用栈
#define PLT_SCOPE() ::nyx::sdk::hook::PltCallScope nyx_plt_scope(__builtin_return_address(0))
