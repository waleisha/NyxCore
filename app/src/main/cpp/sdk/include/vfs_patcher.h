#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace vfs {

// 可跟踪补丁 ID
using PatchId = std::uint64_t;

// VFS 路径策略配置
struct Config {
    // 私有根目录
    const char* private_root = nullptr;
    // 是否拒绝写入重定向目标
    bool read_only = false;
    // 允许访问的额外根目录
    const char* const* allowed_roots = nullptr;
    // 额外根目录数量
    std::size_t allowed_root_count = 0;
    // 是否允许系统常见根目录
    bool allow_common_roots = true;
};

// 路径重定向决策
struct Decision {
    // 原始路径
    std::string path;
    // 实际目标路径
    std::string target;
    // 决策原因
    std::string reason;
    // 是否发生重定向
    bool redirected = false;
};

// 模块补丁状态
enum class PatchStatus {
    Planned,
    Applied,
    RolledBack,
    Failed,
    RollbackFailed,
};

// 模块补丁所在内存段
struct PatchSegment {
    // 段起始地址
    std::uintptr_t start = 0;
    // 段结束地址
    std::uintptr_t end = 0;
    // 段权限字符串
    std::string permissions;
    // 文件偏移
    std::uintptr_t offset = 0;
    // 映射路径
    std::string path;
};

// 模块补丁记录：保存回滚所需的原始字节
struct ModulePatchRecord {
    // 补丁 ID
    std::uint64_t id = 0;
    // 模块名
    std::string module;
    // 文件相对偏移
    std::uintptr_t file_offset = 0;
    // 运行时地址
    std::uintptr_t runtime_address = 0;
    // 写入前字节
    std::vector<std::uint8_t> before;
    // 写入后字节
    std::vector<std::uint8_t> after;
    // 所在内存段
    PatchSegment segment;
    // 当前补丁状态
    PatchStatus status = PatchStatus::Planned;
    // 最近一次操作结果
    Result result;
};

// 初始化 VFS 策略
NYX_EXPORT Result Init(const Config& config);
// 添加路径重定向规则
NYX_EXPORT Result Redirect(const char* from, const char* to);
// 移除路径重定向规则
NYX_EXPORT Result Remove(const char* from);
// 获取路径重定向决策
NYX_EXPORT Result GetRedirect(const char* path, int flags, Decision* out);
// 按模块名和文件相对偏移执行一次性补丁
NYX_EXPORT Result Patch(const char* module, std::uintptr_t offset, const void* data, std::size_t size);
// 按模块名和文件相对偏移写入可平凡复制的值
template <class T>
Result Patch(const char* module, std::uintptr_t offset, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "vfs::Patch requires trivially copyable values");
    return Patch(module, offset, &value, sizeof(T));
}
// 执行模块补丁并返回补丁记录
NYX_EXPORT Result Patch(
    const char* module,
    std::uintptr_t offset,
    const void* data,
    std::size_t size,
    ModulePatchRecord* out
);
// 执行模块补丁并登记为可按 ID 回滚
NYX_EXPORT Value<PatchId> PatchTracked(
    const char* module,
    std::uintptr_t offset,
    const void* data,
    std::size_t size
);
// 执行可跟踪的值补丁
template <class T>
Value<PatchId> PatchTracked(const char* module, std::uintptr_t offset, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "vfs::PatchTracked requires trivially copyable values");
    return PatchTracked(module, offset, &value, sizeof(T));
}
// 按 tracked patch ID 回滚
NYX_EXPORT Result Rollback(PatchId id);
// 按补丁记录回滚模块字节
NYX_EXPORT Result Rollback(ModulePatchRecord* record);

} // namespace vfs
} // namespace sdk
} // namespace nyx
