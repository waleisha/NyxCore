#include "src/app_bridge/auth_config.h"

namespace nyx {
namespace app_bridge {

// 默认使用 SDK 内部配置项。
sdk::auth::InitConfig default_auth_config() {
    return sdk::auth::InitConfig{};
}

} // namespace app_bridge
} // namespace nyx
