#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "sdk/include/utils.h"

namespace nyx {
namespace sdk {
namespace memory {

// 内存区域：按 /proc/maps 分类过滤
inline constexpr int RANGE_ALL = 0;
inline constexpr int RANGE_JAVA_HEAP = 2;
inline constexpr int RANGE_C_HEAP = 1;
inline constexpr int RANGE_C_ALLOC = 4;
inline constexpr int RANGE_C_DATA = 8;
inline constexpr int RANGE_C_BSS = 16;
inline constexpr int RANGE_ANONYMOUS = 32;
inline constexpr int RANGE_JAVA = 65536;
inline constexpr int RANGE_STACK = 64;
inline constexpr int RANGE_ASHMEM = 524288;
inline constexpr int RANGE_VIDEO = 1048576;
inline constexpr int RANGE_OTHER = -2080896;
inline constexpr int RANGE_B_BAD = 131072;
inline constexpr int RANGE_CODE_APP = 16384;
inline constexpr int RANGE_CODE_SYSTEM = 32768;

// 模块头部类型：XA 为代码段，CD/CB 为相邻数据段
inline constexpr int HEAD_XA = 0;
inline constexpr int HEAD_CD = 1;
inline constexpr int HEAD_CB = 2;

// 标量值类型
inline constexpr int TYPE_DWORD = 4;
inline constexpr int TYPE_FLOAT = 16;
inline constexpr int TYPE_DOUBLE = 64;
inline constexpr int TYPE_WORD = 2;
inline constexpr int TYPE_BYTE = 1;
inline constexpr int TYPE_QWORD = 32;

// maps 条目
struct MapEntry {
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址
    std::uintptr_t end = 0;
    // 权限字符串
    std::string permissions;
    // 文件偏移
    std::uintptr_t offset = 0;
    // 设备号
    std::string device;
    // inode 编号
    std::uint64_t inode = 0;
    // 映射路径
    std::string path;
};

// 动态库映射信息
struct Library {
    // 库名
    std::string name;
    // 库路径
    std::string path;
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址
    std::uintptr_t end = 0;
    // 所有映射段
    std::vector<MapEntry> segments;
};

// VMA 快照
struct VmaSnapshot {
    // 快照 ID
    std::uint64_t id = 0;
    // 快照时间
    std::uint64_t monotonic_time_ns = 0;
    // 快照中的 maps 条目
    std::vector<MapEntry> entries;
};

// VMA 差异类型
enum class VmaDiffKind {
    Added,
    Removed,
    RangeChanged,
    PermissionChanged,
    NameChanged,
    Unchanged,
};

// 单条 VMA 差异
struct VmaDiffEntry {
    // 差异类型
    VmaDiffKind kind = VmaDiffKind::Unchanged;
    // 变更前条目
    MapEntry before;
    // 变更后条目
    MapEntry after;
    // 差异说明
    std::string detail;
};

// VMA 快照差异
struct VmaDiff {
    // 变更前快照
    VmaSnapshot before;
    // 变更后快照
    VmaSnapshot after;
    // 差异条目列表
    std::vector<VmaDiffEntry> entries;
};

// 页权限
struct PagePermission {
    // 是否可读
    bool read = false;
    // 是否可写
    bool write = false;
    // 是否可执行
    bool execute = false;
};

// 内存写入策略
enum class WritePolicy {
    // 只写本来可写的页面
    CurrentPermission,
    // 写入前临时放开权限，写完恢复
    AutoProtect,
    // 写完后额外确认权限已恢复
    SecureWrite,
};

// 搜索或冻结的标量项
struct Item {
    // 字符串形式的值
    std::string value;
    // 目标地址
    std::uintptr_t addr = 0;
    // 值类型
    int type = TYPE_DWORD;
};

// 内存工具上下文：保存目标进程、搜索结果和冻结项
struct MemTool {
    // 目标 pid
    int pid = -1;
    // 目标包名
    std::string packageName;
    // 预定义内存区域
    int memoryArea = RANGE_ALL;
    // 自定义 maps 过滤条件
    std::string memoryMaps;
    // 当前搜索结果
    std::vector<std::uintptr_t> results;
    // 当前冻结项
    std::vector<Item> freezeItems;
    // 冻结循环间隔（毫秒）
    std::uint32_t freezeDelayMs = 200;
    // 搜索结果上限，0 表示不限制
    std::size_t maxResults = 0;
};

// 页面保护选项
struct ProtectOptions {
    // 起始地址
    std::uintptr_t start = 0;
    // 结束地址
    std::uintptr_t end = 0;
    // 是否可读
    bool read = true;
    // 是否可写
    bool write = false;
    // 是否可执行
    bool execute = false;
};

// VMA 重映射请求
struct RemapRequest {
    // 原始地址
    std::uintptr_t start = 0;
    // 原始大小
    std::size_t size = 0;
    // 新映射权限
    PagePermission permission;
    // 是否保留原内容
    bool preserve_content = true;
    // 新匿名 VMA 名称
    std::string anon_name;
};

// VMA 操作类型
enum class OperationKind {
    Protect,
    SetName,
    Advise,
    Resize,
    Remap,
};

// VMA 操作状态
enum class OperationStatus {
    Planned,
    Applied,
    RolledBack,
    Failed,
    RollbackFailed,
};

// VMA 操作记录
struct OperationRecord {
    // 操作 ID
    std::uint64_t id = 0;
    // 所属事务 ID
    std::uint64_t transaction_id = 0;
    // 操作类型
    OperationKind kind = OperationKind::Protect;
    // 操作状态
    OperationStatus status = OperationStatus::Planned;
    // 操作前 maps 条目
    MapEntry before;
    // 操作后 maps 条目
    MapEntry after;
    // 原始权限
    ProtectOptions original_protection;
    // 应用后的权限
    ProtectOptions applied_protection;
    // 匿名 VMA 名称
    std::string anon_name;
    // madvise 参数
    int advice = 0;
    // mremap 标志
    int mremap_flags = 0;
    // 原始地址
    std::uintptr_t old_address = 0;
    // 新地址
    std::uintptr_t new_address = 0;
    // 原始大小
    std::size_t old_size = 0;
    // 新大小
    std::size_t new_size = 0;
    // 操作结果
    Result result;
};

// 获取当前进程 maps
NYX_EXPORT Result GetMaps(std::vector<MapEntry>* out);
// 获取当前进程 maps，失败信息保留在 Value 中
NYX_EXPORT Value<std::vector<MapEntry>> TryGetMaps();
// 获取当前进程 maps，失败时返回空列表
NYX_EXPORT std::vector<MapEntry> GetMaps();
// 获取当前进程动态库列表
NYX_EXPORT Result GetLibs(std::vector<Library>* out);
// 获取当前进程动态库列表，失败信息保留在 Value 中
NYX_EXPORT Value<std::vector<Library>> TryGetLibs();
// 获取当前进程动态库列表，失败时返回空列表
NYX_EXPORT std::vector<Library> GetLibs();
// 查找地址所在 maps 条目
NYX_EXPORT Result FindAddr(std::uintptr_t address, MapEntry* out);
// 按名称查找动态库 maps 条目
NYX_EXPORT Result FindLib(const char* name, std::vector<MapEntry>* out);
// 捕获当前 VMA 快照
NYX_EXPORT Result Snapshot(VmaSnapshot* out);
// 比较两次 VMA 快照
NYX_EXPORT Result Diff(const VmaSnapshot& before, const VmaSnapshot& after, VmaDiff* out);
// 通过事务提交单次权限保护
NYX_EXPORT Result Protect(const ProtectOptions& options, OperationRecord* out = nullptr);
// 按布尔权限保护内存区域
NYX_EXPORT Result Protect(void* addr, std::size_t len, bool read, bool write, bool execute);
// 按权限结构保护内存区域
NYX_EXPORT Result Protect(void* addr, std::size_t len, PagePermission permission, OperationRecord* out = nullptr);
// 保护指定库代码段
NYX_EXPORT Result ProtectLib(const char* name, std::vector<OperationRecord>* out);
// 写入当前进程内存
NYX_EXPORT Result Write(
    void* addr,
    const void* data,
    std::size_t size,
    WritePolicy policy = WritePolicy::CurrentPermission
);

// 写入可平凡复制的值
template <class T>
Result Write(void* addr, const T& value, WritePolicy policy = WritePolicy::CurrentPermission) {
    static_assert(std::is_trivially_copyable<T>::value, "memory::Write requires trivially copyable values");
    return Write(addr, &value, sizeof(T), policy);
}

// 临时放开权限后写入值
template <class T>
Result SafeWrite(void* addr, const T& value) {
    return Write(addr, value, WritePolicy::AutoProtect);
}

// 按包名查找 pid
NYX_EXPORT Value<int> getPID(const char* packageName);
// 将 MemTool 绑定到包名对应的进程
NYX_EXPORT Result setPackageName(MemTool* mem, const char* packageName);
// 将 MemTool 绑定到指定 pid
NYX_EXPORT Result setPid(MemTool* mem, int pid);
// 设置预定义内存区域
NYX_EXPORT Result setArea(MemTool* mem, int memoryArea);
// 设置自定义 maps 过滤条件
NYX_EXPORT Result setArea(MemTool* mem, const char* memoryMaps);

// 查询模块基址
NYX_EXPORT Value<std::uintptr_t> getModuleBaseAddr(
    const MemTool& mem,
    const char* moduleName,
    int headType = HEAD_XA
);
// 读取指针宽度的数据作为跳转地址
NYX_EXPORT Value<std::uintptr_t> jump(const MemTool& mem, std::uintptr_t addr, int count);
// 读取 32 位指针
NYX_EXPORT Value<std::uintptr_t> jump32(const MemTool& mem, std::uintptr_t addr);
// 读取 64 位指针
NYX_EXPORT Value<std::uintptr_t> jump64(const MemTool& mem, std::uintptr_t addr);

// 写入地址值，isFree 为真时转为冻结项
NYX_EXPORT Result setAddrValue(
    MemTool* mem,
    const char* value,
    std::uintptr_t addr,
    int type,
    bool isFree = false,
    WritePolicy policy = WritePolicy::AutoProtect
);
// 读取地址值并格式化为字符串
NYX_EXPORT Value<std::string> getAddrData(const MemTool& mem, std::uintptr_t addr, int type);

// 搜索单值，含分号时转入联合搜索
NYX_EXPORT Value<std::vector<std::uintptr_t>> Search(MemTool* mem, const char* value, int type);
// 搜索范围值
NYX_EXPORT Value<std::vector<std::uintptr_t>> SearchRange(MemTool* mem, const char* value, int type);
// 搜索联合条件
NYX_EXPORT Value<std::vector<std::uintptr_t>> SearchUnited(MemTool* mem, const char* value, int type);
// 按偏移筛选已有搜索结果
NYX_EXPORT Value<std::vector<std::uintptr_t>> ImproveOffset(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset
);
// 按范围值筛选已有搜索结果
NYX_EXPORT Value<std::vector<std::uintptr_t>> ImproveOffsetRange(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset
);
// 按联合条件筛选已有搜索结果
NYX_EXPORT Value<std::vector<std::uintptr_t>> ImproveOffsetUnited(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset
);
// 用零偏移筛选已有搜索结果
NYX_EXPORT Value<std::vector<std::uintptr_t>> ImproveValue(MemTool* mem, const char* value, int type);
// 对当前结果集按偏移写入或冻结
NYX_EXPORT Result OffsetWrite(
    MemTool* mem,
    const char* value,
    int type,
    std::intptr_t offset,
    bool isFree = false,
    WritePolicy policy = WritePolicy::AutoProtect
);

// 获取当前结果数量
NYX_EXPORT std::size_t getResultCount(const MemTool& mem);
// 获取当前结果列表引用
NYX_EXPORT const std::vector<std::uintptr_t>& getResultList(const MemTool& mem);
// 清空当前结果列表
NYX_EXPORT Result clearResultList(MemTool* mem);
// 获取当前冻结项列表
NYX_EXPORT Value<std::vector<Item>> getFreezeList(const MemTool& mem);
// 设置冻结循环间隔
NYX_EXPORT Result setFreezeDelayMs(MemTool* mem, std::uint32_t delay);
// 获取冻结项数量
NYX_EXPORT std::size_t getFreezeNum(const MemTool& mem);
// 添加冻结项
NYX_EXPORT Result addFreezeItem(MemTool* mem, const char* value, std::uintptr_t addr, int type);
// 移除指定地址的冻结项
NYX_EXPORT Result removeFreezeItem(MemTool* mem, std::uintptr_t addr);
// 移除全部冻结项
NYX_EXPORT Result removeAllFreezeItem(MemTool* mem);
// 启动全部冻结项
NYX_EXPORT Result startAllFreeze(MemTool* mem);
// 停止全部冻结项
NYX_EXPORT Result stopAllFreeze(MemTool* mem);
// 设置匿名 VMA 名称
NYX_EXPORT Result SetName(
    std::uintptr_t start,
    std::size_t size,
    const char* name,
    OperationRecord* out = nullptr
);
// 对 VMA 执行 madvise
NYX_EXPORT Result Advise(std::uintptr_t start, std::size_t size, int advice, OperationRecord* out = nullptr);
// 调整 VMA 大小
NYX_EXPORT Result Resize(
    std::uintptr_t start,
    std::size_t old_size,
    std::size_t new_size,
    int flags,
    std::uintptr_t* out_new_address,
    OperationRecord* out = nullptr
);
// 重映射 VMA
NYX_EXPORT Result Remap(
    const RemapRequest& request,
    std::uintptr_t* out_new_address,
    OperationRecord* out = nullptr
);
// 回滚指定事务
NYX_EXPORT Result Rollback(std::uint64_t transaction_id, std::vector<OperationRecord>* out);

} // namespace memory
} // namespace sdk
} // namespace nyx
