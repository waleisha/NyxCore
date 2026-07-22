#pragma once

#include <string>
#include <vector>

namespace nyx {
namespace runtime {
namespace vfs {

// VFS 路径访问策略
struct PathPolicy {
    // 私有根目录
    std::string private_root;
    // 额外允许的根目录
    std::vector<std::string> allowed_roots;
    // 是否禁止写入打开
    bool read_only = false;
    // 是否允许常用公共根目录
    bool allow_common_roots = true;

    // 判断重定向目标是否符合策略
    bool allows(const std::string& from, const std::string& to, std::string* reason = nullptr) const;
    // 判断打开路径和 flags 是否符合策略
    bool allows_open(const std::string& path, int flags, std::string* reason = nullptr) const;
};

} // namespace vfs
} // namespace runtime
} // namespace nyx
