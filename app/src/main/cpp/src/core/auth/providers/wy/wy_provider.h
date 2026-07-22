#pragma once

#include "src/core/auth/auth_types.h"
#include "src/core/auth/providers/wy/wy_profile.h"

#include <memory>

namespace nyx {
namespace core {
namespace auth {
namespace wy {

// 使用明文 profile 创建 WY provider
std::unique_ptr<IProvider> make_provider(Profile profile);
// 使用运行时 profile 创建 WY provider
std::unique_ptr<IProvider> make_provider(RuntimeProfile profile);

} // namespace wy

// 使用默认配置创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider();
// 使用明文 profile 创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider(wy::Profile profile);
// 使用运行时 profile 创建 WY provider
std::unique_ptr<IProvider> MakeWyProvider(wy::RuntimeProfile profile);

} // namespace auth
} // namespace core
} // namespace nyx
