<div align="center">

# NyxCore

**面向 Android Native MOD 的高性能、模块化 SDK 开源工程**

![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)
![Language](https://img.shields.io/badge/Language-C%2B%2B20%20%7C%20Kotlin-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Android-green.svg)
![NDK](https://img.shields.io/badge/NDK-r30--beta1-brightgreen.svg)
![Build](https://img.shields.io/badge/Gradle-8.13-02569B.svg)

[快速开始](#-快速开始) • [SDK 能力速览](#-sdk-能力速览) • [MOD 开发指南](#-mod-开发模型) • [构建指南](#-构建环境) • [安全规范](#-安全与贡献规范)

</div>

---

> **仓库定位**：本仓库提供可审计的 Demo 样板与 SDK 构建骨架。对外提供 `sdk/include/` 公共 SDK 契约，默认公开示例为 `mods/demo`。**本仓库严禁承载任何真实业务资源、私钥、授权凭据或服务端敏感字段。**

---

## 仓库定位

| 配置项 | 当前设定 / 路径 |
| :--- | :--- |
| **项目定位** | Android Native MOD SDK / Runtime 示例工程 |
| **默认公开 MOD** | `mods/demo` |
| **对外 SDK 契约** | `app/src/main/cpp/sdk/include/` |
| **默认验证模式** | `config/active.properties` (`mode=release`) |
| **发布门槛** | `mod.properties` (`publish=true`) |
| **推荐检查入口** | `.\gradlew.bat :app:nyxInfo` / `.\gradlew.bat :app:nyxMods` |

---

## 目录

- [仓库定位](#仓库定位)
- [快速开始](#快速开始)
- [项目特色](#项目特色)
- [开源范围](#开源范围)
- [SDK 能力速览](#sdk-能力速览)
- [MOD 开发模型](#mod-开发模型)
- [SDK API 参考](#sdk-api-参考)
- [工程结构](#工程结构)
- [构建环境](#构建环境)
- [常用命令](#常用命令)
- [切换 MOD](#切换-mod)
- [边界约定](#边界约定)
- [开源发布清单](#开源发布清单)
- [贡献](#贡献)
- [安全](#安全)
- [许可证](#许可证)

## 快速开始

1. 安装 Android Studio，并准备 JDK 17、Android SDK 36 和 CMake 3.22.1+。
2. 下载 [ALLVM v1.2.0](https://github.com/abcdefgjh-li/ALLVM/releases/tag/v1.2.0) 里的 `android-ndk-r30-beta1-windows.zip`，解压到固定目录，例如 `D:/Android/android-ndk-r30-beta1-windows`。
3. 如果 Android Studio 没有自动生成 `local.properties`，复制 `local.properties.example` 为 `local.properties`，把 `sdk.dir` 和 `nyx.ndk.dir` 改成本机路径。也可以不写文件，直接用 `-PnyxNdkPath=<path>`、`ANDROID_NDK_HOME` 或 `ANDROID_NDK_ROOT`。
4. 先跑一次检查和示例构建：

```powershell
.\gradlew.bat :app:nyxInfo
.\gradlew.bat :app:nyxMods
.\gradlew.bat :app:assembleDebug -PnyxMod=demo
.\gradlew.bat :app:testDebugUnitTest
```

如需产出发布包，再运行 `.\gradlew.bat :app:assembleRelease`。Release 构建会启用 ALLVM 混淆参数，必须使用上面的 ALLVM-patched NDK。

## 项目特色

- SDK 优先：业务模块只依赖 `app/src/main/cpp/sdk/include/`，通过 `Result` / `Value<T>` 获取状态和值，不直接触碰 `src/runtime/`、`src/engines/` 或 vendor 头文件。
- Runtime 聚合：Hook、Memory、VFS、Stack、Loader 等能力统一封装；底层不可用、未授权或 ABI 不支持时返回明确状态。
- Engine 只读解析：Unity / Unreal 以探测、查询、签名校验和 binding event 为主，Unity 当前重点覆盖 IL2CPP readonly resolver。
- Demo 可交互：Android 侧通过 `GLSurfaceView` 承载 Native 渲染，C++ 侧通过 ImGui 暴露登录、开关、滑杆和日志示例。
- 授权链路内置：`sdk::auth` 已接入 WY profile、登录、登出、变量、公告和更新检查，Java/Kotlin bridge 会把耗时授权请求放到后台线程。
- 三方依赖可审计：ImGui、Dobby、ByteHook、ShadowHook、Mbed TLS、Monocypher、cpp-httplib、nlohmann/json 等依赖都按本地快照和 License 清单维护。

## 仓库范围

本仓库定位为 Android Native MOD SDK / Runtime 开源工程，而非具体业务 MOD 的完整发布仓库。

### 已公开内容

- `app/src/main/cpp/sdk/include/`：对外 SDK 契约。
- `app/src/main/cpp/src/`：SDK 背后的 Runtime、Engine、Overlay、Auth、Network、Crypto 和测试实现。
- `app/src/main/cpp/mods/demo/`：最小可构建 MOD 样板。
- `app/src/main/kotlin/dev/nyxcore/manager/`：Android 示例 App、登录 UI、Native bridge 和 `GLSurfaceView` 容器。
- `app/src/main/cpp/vendor/README.md`：三方依赖版本、来源和许可证清单。

### 不纳入仓库的内容

- 真实业务 MOD 目录、私有资源包、payload、偏移、签名、公钥/私钥、授权配置和服务端字段映射。
- `local.properties`、`keystores/` 下的真实签名文件、`secure/` 目录和任何 `*.local.properties` / `*.secret.properties`。
- 任何无法确认授权来源的第三方素材、游戏资源、二进制载荷或调试日志。
## SDK 能力速览

| 头文件 | 能力 |
| --- | --- |
| `mod.h` | MOD 生命周期、模块注册、`ModEntry()` 入口约定。 |
| `ui.h` | ImGui wrapper：窗口、文本、按钮、输入框、滑杆、字体、GIF 和基础布局。 |
| `auth.h` | Android Context 初始化、登录 / 登出、会话状态、特性判断、变量 / 公告 / 更新检查。 |
| `hook.h` | Inline / PLT hook、模块 offset / symbol 解析、卸载、调用作用域和 hook 记录查询。 |
| `memory.h` | maps / library 查询、地址定位、页权限、写入策略、搜索改值、冻结列表、VMA snapshot / diff、回滚记录。 |
| `vfs_patcher.h` | 路径重定向、当前进程模块 patch、tracked patch id 和 rollback。 |
| `engine.h` | Unity / Unreal probe、image / class / method / field 查询、方法路径查找、签名 binding event。 |
| `stack.h` | 调用栈采样、raw / normalized frame、Hook frame 标注和只读诊断输出。 |
| `network.h` | HTTPS GET / POST、超时、CA bundle、主线程请求保护和 body 长度限制。 |
| `crypto.h` | MD5、SHA-256、Blake2b、RC4、XChaCha20-Poly1305、AES-256-GCM 解密。 |
| `test.h` | Native doctor、integration gate 和 benchmark scope timer。 |
| `utils.h` | 日志、主线程任务队列、状态字符串、运行时 uptime。 |

## MOD 开发模型

业务代码放在 `app/src/main/cpp/mods/<name>/`，通过 SDK 头文件表达依赖，并在 `ModEntry()` 中注册模块：

```cpp
#include "sdk/include/mod.h"
#include "sdk/include/ui.h"
#include "sdk/include/utils.h"

class DemoMod final : public nyx::sdk::IMod {
public:
    void OnInit() override {
        NYX_LOGI("demo initialized");
    }

    void OnUpdate() override {}

    void OnDraw() override {
        if (!nyx::sdk::ui::Begin("NyxCore Demo")) {
            return;
        }
        nyx::sdk::ui::Text("SDK driven native module");
        nyx::sdk::ui::End();
    }
};
```

`mods/demo/src/main.cpp` 展示了最小可发布样板：模块注册、生命周期回调、ImGui 窗口、文本、开关、滑杆和日志输出。

## SDK API 参考

SDK 公共头文件位于 `app/src/main/cpp/sdk/include/`。业务 MOD 只需要 include 这里的头文件，不要直接依赖 `src/`、`vendor/` 或具体 Hook / ImGui / IL2CPP 后端。

### 通用约定

- 头文件统一按项目根 include：`#include "sdk/include/<name>.h"`。
- 命名空间以 `nyx::sdk` 为根，具体能力在 `nyx::sdk::ui`、`nyx::sdk::hook`、`nyx::sdk::memory` 等子命名空间下。
- 大多数 SDK API 返回 `nyx::sdk::Result` 或 `nyx::sdk::Value<T>`；用 `ok()` 判断是否成功，失败时读 `result.status` 和 `result.detail`。
- `auth.h` 有独立的 `nyx::sdk::auth::Result`，用 `success` 判断是否成功，失败时读 `code`、`failure` 和 `message`。
- `Try*` 函数会保留错误信息；不带 `Try` 的便捷函数通常失败时返回空值或 `nullptr`，适合简单场景。
- 网络、授权登录这类耗时调用不要放在 UI / GL 主线程；`network.h` 会直接拒绝主线程请求。
- Hook、Memory、VFS、Engine 能力仅用于自有 App、自有 Demo、授权测试环境和明确授权的 MOD 场景。

通用返回值示例：

```cpp
#include "sdk/include/memory.h"
#include "sdk/include/utils.h"

void DumpMapsCount() {
    auto maps = nyx::sdk::memory::TryGetMaps();
    if (!maps.ok()) {
        NYX_LOGW(
            "GetMaps failed: status=%s detail=%s",
            nyx::sdk::StatusStr(maps.result.status),
            maps.result.detail.c_str()
        );
        return;
    }

    NYX_LOGI("maps count=%zu", maps.value.size());
}
```

### 最小 MOD 模板

每个 MOD 暴露一个 `extern "C" void ModEntry()`，在里面调用 `nyx::sdk::Register` 注册工厂。`OnInit()` 初始化状态，`OnUpdate()` 放每帧逻辑，`OnDraw()` 绘制 UI。

```cpp
#include "sdk/include/mod.h"
#include "sdk/include/ui.h"
#include "sdk/include/utils.h"

#include <memory>

namespace {

class ExampleMod final : public nyx::sdk::IMod {
public:
    void OnInit() override {
        NYX_LOGI("example mod initialized");
    }

    void OnUpdate() override {}

    void OnDraw() override {
        if (!nyx::sdk::ui::Begin("Example Mod")) {
            return;
        }

        nyx::sdk::ui::Text("Hello from NyxCore SDK");
        nyx::sdk::ui::End();
    }
};

std::unique_ptr<nyx::sdk::IMod> CreateExampleMod() {
    return std::make_unique<ExampleMod>();
}

} // namespace

extern "C" NYX_EXPORT void ModEntry() {
    nyx::sdk::Info info;
    info.name = "example";
    info.feature = nullptr;
    info.enabled_by_default = true;
    nyx::sdk::Register(info, CreateExampleMod);
}
```

### `ui.h`：ImGui 封装

常用接口：

| API | 用途 |
| --- | --- |
| `Begin(title, open)` / `End()` | 创建和结束窗口；`Begin()` 返回 `false` 时内部已处理 `End()`。 |
| `Text()` / `Button()` / `Checkbox()` | 文本、按钮、复选框。 |
| `Slider()` / `InputText()` | 浮点滑杆和文本输入。 |
| `LoadFont()` / `LoadFontFile()` / `PushFont()` / `PopFont()` | 注册并使用字体。 |
| `Gif()` / `ReleaseGif()` | 绘制或释放 GIF 帧缓存。 |
| `Line()` / `SameLine()` / `SetSize()` / `PushColor()` | 基础布局和文本颜色。 |

UI 状态一般放在 MOD 成员或静态状态中，不要每帧重置输入缓冲区。

```cpp
#include "sdk/include/mod.h"
#include "sdk/include/ui.h"

class UiDemo final : public nyx::sdk::IMod {
public:
    void OnInit() override {}
    void OnUpdate() override {}

    void OnDraw() override {
        nyx::sdk::ui::SetSize(520.0f, 360.0f);
        if (!nyx::sdk::ui::Begin("SDK UI")) {
            return;
        }

        nyx::sdk::ui::Text(enabled_ ? "Feature enabled" : "Feature disabled");
        nyx::sdk::ui::Checkbox("Enable feature", &enabled_);
        nyx::sdk::ui::Slider("Speed", &speed_, 0.0f, 10.0f);

        if (nyx::sdk::ui::Button("Apply")) {
            applied_speed_ = speed_;
        }

        nyx::sdk::ui::End();
    }

private:
    bool enabled_ = false;
    float speed_ = 1.0f;
    float applied_speed_ = 1.0f;
};
```

### `auth.h`：授权、变量、公告和更新检查

调用流程：

1. 在 Java/Kotlin bridge 拿到 `JNIEnv*` 和 Android `Context` 后调用 `auth::Init(env, context, config)`。
2. 在后台线程调用 `auth::Login(license)`。
3. 用 `auth::IsLoggedIn()`、`auth::CanRun(feature)` 控制功能开关。
4. 用 `TryGetVar()`、`TryGetNotice()`、`TryCheckUpdate()` 获取远端变量、公告和更新信息。

需要接入 WY 授权的 MOD 可以把 `mod.properties` 的 `auth` 设为 `wy`，并在自己的 `config/auth.properties` 中提供 profile 字段。

```cpp
#include "sdk/include/auth.h"
#include "sdk/include/utils.h"

#include <string>
#include <thread>
#include <utility>

void InitAuth(JNIEnv* env, jobject android_context, const nyx::sdk::auth::InitConfig& config) {
    auto result = nyx::sdk::auth::Init(env, android_context, config);
    if (!result.success) {
        NYX_LOGE("auth init failed: code=%d message=%s", result.code, result.message.c_str());
    }
}

void LoginOnWorker(std::string license) {
    std::thread([license = std::move(license)]() {
        auto result = nyx::sdk::auth::Login(license.c_str());
        if (!result.success) {
            NYX_LOGE("login failed: code=%d message=%s", result.code, result.message.c_str());
            return;
        }

        auto remote_var = nyx::sdk::auth::TryGetVar("payload_seed");
        if (remote_var.ok()) {
            NYX_LOGI("payload_seed=%s", remote_var.value.c_str());
        }
    }).detach();
}

bool CanUseFeature() {
    return nyx::sdk::auth::IsLoggedIn() &&
           nyx::sdk::auth::CanRun("example_feature");
}
```

### `utils.h`：日志、主线程任务和运行时信息

常用接口：

| API | 用途 |
| --- | --- |
| `NYX_LOGD/I/W/E` | Debug / Info / Warning / Error 日志。 |
| `utils::Post(task)` | 从工作线程投递任务到主调度队列。 |
| `utils::RunTasks()` | 执行队列中的任务；框架渲染循环已在 ImGui bridge 中调用。 |
| `utils::TaskCount()` | 查询待执行任务数。 |
| `utils::IsMain()` | 判断当前线程是否为 NyxCore 主线程。 |
| `utils::Uptime()` | 返回运行时启动后的微秒数。 |

```cpp
#include "sdk/include/utils.h"

void RunAsyncWork() {
    NYX_LOGI("uptime=%llu", static_cast<unsigned long long>(nyx::sdk::utils::Uptime()));

    nyx::sdk::utils::Post([]() {
        NYX_LOGI("back on dispatcher, is_main=%d", nyx::sdk::utils::IsMain() ? 1 : 0);
    });
}
```

### `network.h`：HTTPS GET / POST

`net::GetConfig()` 和 `net::Post()` 只接受 `https://` URL，并拒绝在主线程发请求。`HttpResponse.status` 是 HTTP 状态码，`error_code == 0` 表示网络层没有错误；非 2xx HTTP 状态会写入 `error_code`。

```cpp
#include "sdk/include/network.h"
#include "sdk/include/utils.h"

#include <thread>

void FetchRemoteConfig() {
    std::thread([]() {
        auto response = nyx::sdk::net::GetConfig(
            "https://example.com",
            "/config.json",
            3,
            5
        );

        if (response.error_code != 0) {
            NYX_LOGW("config request failed: status=%d error=%d", response.status, response.error_code);
            return;
        }

        NYX_LOGI("config body length=%zu", response.body.size());
    }).detach();
}
```

POST 示例：

```cpp
auto response = nyx::sdk::net::Post(
    "https://example.com",
    "/api/report",
    "event=boot&value=1",
    3,
    5,
    4096
);
```

### `crypto.h`：摘要、RC4、XChaCha20-Poly1305、AES-256-GCM

常用接口：

| API | 用途 |
| --- | --- |
| `Md5()` / `Sha256()` / `Blake2b()` | 返回十六进制摘要字符串。 |
| `Md5Bytes()` / `Sha256Bytes()` / `Blake2bBytes()` | 返回原始摘要字节。 |
| `Rc4(key, key_len, data, data_len)` | 原地 RC4 处理。 |
| `ChaCha20(payload, len, key, nonce, mac)` | 原地 XChaCha20-Poly1305 加密并输出 16 字节 MAC。 |
| `AesDecrypt()` / `AesDecryptRaw()` | AES-256-GCM 解密。 |

```cpp
#include "sdk/include/crypto.h"
#include "sdk/include/utils.h"

#include <string>
#include <vector>

void CryptoExample() {
    const std::string digest = nyx::sdk::crypt::Sha256("hello");
    NYX_LOGI("sha256=%s", digest.c_str());

    std::vector<std::uint8_t> cipher = {/* ciphertext */};
    std::vector<std::uint8_t> key(32, 0);
    std::vector<std::uint8_t> iv(12, 0);
    std::vector<std::uint8_t> mac(16, 0);

    auto plain = nyx::sdk::crypt::AesDecrypt(cipher, key, iv, mac);
    if (!plain.ok()) {
        NYX_LOGW("aes decrypt failed: %s", plain.result.detail.c_str());
        return;
    }
}
```

### `hook.h`：Inline / PLT Hook

常用接口：

| API | 用途 |
| --- | --- |
| `ResolveOffset(library, offset)` | 把已加载动态库的相对偏移解析为绝对地址。 |
| `ResolveSymbol(library, symbol)` | 在已加载动态库中查找导出符号。 |
| `Inline()` / `InlineOffset()` / `InlineSymbol()` | 安装 inline hook，成功时可回填原函数跳板。 |
| `UnhookInline*()` | 卸载 inline hook。 |
| `Plt()` / `UnhookPlt()` | 安装或卸载 PLT hook。 |
| `Get()` | 读取 Hook 记录、命中次数、失败次数和状态。 |

Inline Hook 示例：

```cpp
#include "sdk/include/hook.h"
#include "sdk/include/utils.h"

using ProbeFn = int (*)(int);
ProbeFn g_original_probe = nullptr;

int ProbeReplacement(int value) {
    const int base = g_original_probe != nullptr ? g_original_probe(value) : value;
    return base + 1;
}

void InstallInlineHook() {
    auto target = nyx::sdk::hook::ResolveSymbol("libnyx_test_probe.so", "nyx_runtime_probe_value");
    if (!target.ok()) {
        NYX_LOGW("resolve failed: %s", target.result.detail.c_str());
        return;
    }

    auto result = nyx::sdk::hook::Inline<ProbeFn>(
        target.value,
        ProbeReplacement,
        &g_original_probe
    );
    if (!result.ok()) {
        NYX_LOGW("inline hook failed: %s", result.detail.c_str());
    }
}
```

PLT Hook 示例。replacement 里如果要调用原函数，先创建 `PLT_SCOPE()`，用于恢复 ByteHook 调用栈。

```cpp
#include "sdk/include/hook.h"

#include <unistd.h>

using GetPidFn = pid_t (*)();
GetPidFn g_real_getpid = nullptr;

pid_t HookedGetPid() {
    PLT_SCOPE();
    return g_real_getpid != nullptr ? g_real_getpid() : -1;
}

void InstallPltHook() {
    auto result = nyx::sdk::hook::Plt(
        "getpid",
        HookedGetPid,
        &g_real_getpid,
        "libnyx_test_probe.so"
    );
    if (!result.ok()) {
        NYX_LOGW("plt hook failed: %s", result.detail.c_str());
    }
}
```

读取 Hook 状态：

```cpp
auto records = nyx::sdk::hook::Get();
if (records.ok()) {
    for (const auto& record : records.value) {
        NYX_LOGI(
            "hook target=%s hits=%zu failures=%zu detail=%s",
            record.target.c_str(),
            record.hit_count,
            record.failure_count,
            record.detail.c_str()
        );
    }
}
```

### `memory.h`：maps、库查询、读写、搜索、冻结和 VMA 操作

常用接口分组：

| 分组 | API |
| --- | --- |
| maps / library | `GetMaps()`、`TryGetMaps()`、`FindAddr()`、`GetLibs()`、`FindLib()` |
| 保护与写入 | `Protect()`、`ProtectLib()`、`Write()`、`SafeWrite()` |
| MemTool | `setPid()`、`setPackageName()`、`setArea()`、`getModuleBaseAddr()`、`jump32()`、`jump64()` |
| 搜索与改值 | `Search()`、`SearchRange()`、`SearchUnited()`、`ImproveValue()`、`OffsetWrite()` |
| 冻结 | `addFreezeItem()`、`startAllFreeze()`、`stopAllFreeze()`、`removeFreezeItem()` |
| VMA | `Snapshot()`、`Diff()`、`SetName()`、`Advise()`、`Resize()`、`Remap()`、`Rollback()` |

查询当前进程 maps：

```cpp
#include "sdk/include/memory.h"
#include "sdk/include/utils.h"

void PrintLibraries() {
    auto libs = nyx::sdk::memory::TryGetLibs();
    if (!libs.ok()) {
        NYX_LOGW("GetLibs failed: %s", libs.result.detail.c_str());
        return;
    }

    for (const auto& lib : libs.value) {
        NYX_LOGI("lib name=%s start=%p", lib.name.c_str(), reinterpret_cast<void*>(lib.start));
    }
}
```

写入已知地址：

```cpp
int local_flag = 0;
auto result = nyx::sdk::memory::SafeWrite(&local_flag, 1);
if (!result.ok()) {
    NYX_LOGW("write failed: %s", result.detail.c_str());
}
```

使用 `MemTool` 搜索和改值。`TYPE_DWORD` / `TYPE_FLOAT` / `TYPE_DOUBLE` / `TYPE_WORD` / `TYPE_BYTE` / `TYPE_QWORD` 对应常用数值类型；范围搜索用 `10~20`，联合搜索用 `100D;1F:1024` 这类格式，其中 `D/F/E/W/B/Q` 可覆盖单项类型，最后的 `:1024` 是扫描跨度。

```cpp
void SearchAndWrite(int pid) {
    nyx::sdk::memory::MemTool mem;

    auto setup = nyx::sdk::memory::setPid(&mem, pid);
    if (!setup.ok()) {
        NYX_LOGW("setPid failed: %s", setup.detail.c_str());
        return;
    }

    nyx::sdk::memory::setArea(&mem, nyx::sdk::memory::RANGE_C_HEAP);
    mem.maxResults = 64;

    auto hits = nyx::sdk::memory::Search(&mem, "100", nyx::sdk::memory::TYPE_DWORD);
    if (!hits.ok() || hits.value.empty()) {
        NYX_LOGW("search failed or empty: %s", hits.result.detail.c_str());
        return;
    }

    auto write = nyx::sdk::memory::OffsetWrite(
        &mem,
        "200",
        nyx::sdk::memory::TYPE_DWORD,
        0
    );
    if (!write.ok()) {
        NYX_LOGW("offset write failed: %s", write.detail.c_str());
    }
}
```

VMA snapshot / diff 示例：

```cpp
nyx::sdk::memory::VmaSnapshot before;
nyx::sdk::memory::VmaSnapshot after;
nyx::sdk::memory::VmaDiff diff;

if (nyx::sdk::memory::Snapshot(&before).ok()) {
    // 执行需要观测的加载或内存操作。
    nyx::sdk::memory::Snapshot(&after);
    if (nyx::sdk::memory::Diff(before, after, &diff).ok()) {
        NYX_LOGI("vma changed entries=%zu", diff.entries.size());
    }
}
```

### `vfs_patcher.h`：路径重定向和模块 Patch

常用接口：

| API | 用途 |
| --- | --- |
| `Init(config)` | 初始化 VFS 私有根目录、允许访问根目录和只读策略。 |
| `Redirect(from, to)` / `Remove(from)` | 添加或删除路径重定向规则。 |
| `GetRedirect(path, flags, out)` | 查询某个路径是否会被重定向。 |
| `Patch()` | 对当前进程已加载模块的文件相对偏移做 patch。 |
| `PatchTracked()` / `Rollback(id)` | 创建可回滚 patch，并按 id 回滚。 |
| `Rollback(record)` | 按 `ModulePatchRecord` 回滚。 |

路径重定向示例：

```cpp
#include "sdk/include/vfs_patcher.h"
#include "sdk/include/utils.h"

void InitVfs(const char* private_root) {
    const char* allowed[] = {private_root};

    nyx::sdk::vfs::Config config;
    config.private_root = private_root;
    config.allowed_roots = allowed;
    config.allowed_root_count = 1;
    config.allow_common_roots = true;

    auto result = nyx::sdk::vfs::Init(config);
    if (!result.ok()) {
        NYX_LOGW("vfs init failed: %s", result.detail.c_str());
    }
}

void RedirectConfigFile() {
    auto result = nyx::sdk::vfs::Redirect(
        "/sdcard/nyx/config.json",
        "/data/user/0/dev.nyxcore.manager/files/config.json"
    );
    if (!result.ok()) {
        NYX_LOGW("redirect failed: %s", result.detail.c_str());
    }
}
```

Tracked patch 示例：

```cpp
std::uint8_t replacement[] = {0x00};
auto patch = nyx::sdk::vfs::PatchTracked(
    "libnyx_test_probe.so",
    0x100,
    replacement,
    sizeof(replacement)
);
if (patch.ok()) {
    nyx::sdk::vfs::Rollback(patch.value);
}
```

### `engine.h`：Unity / Unreal 探测和只读解析

当前 SDK 对 Unity IL2CPP 提供 image / class / method / field 查询和签名校验；Unreal 当前主要用于探测。

常用接口：

| API | 用途 |
| --- | --- |
| `IsUnity()` / `IsUnreal()` | 探测当前进程引擎状态。 |
| `GetImages()` / `FindImage()` | 枚举或查找 Unity image。 |
| `FindClass()` / `FindMethod()` / `FindField()` | 逐级查询 class / method / field。 |
| `TryFindMethod()` / `FindMethod()` | 按路径或字段快速查找方法。 |
| `MethodPtr()` | 从 `Method` 获取方法指针。 |
| `BindMethod()` / `BindField()` | 用签名做只读校验绑定。 |
| `GetEvents()` / `ClearEvents()` / `DroppedEvents()` | 获取 binding event 诊断。 |
| `ClearCache()` | 清理 resolver / binding 缓存。 |

`TryFindMethod(path)` 路径格式为：

```text
image::Namespace.Class::Method(arg_count)
image::Class::Method(*)
```

其中 namespace 可省略，`arg_count` 可省略或写 `*` 表示不限制参数数量。

```cpp
#include "sdk/include/engine.h"
#include "sdk/include/utils.h"

void ResolveUnityMethod() {
    auto probe = nyx::sdk::engine::IsUnity();
    if (!probe.available()) {
        NYX_LOGW("unity unavailable: %s", probe.detail.c_str());
        return;
    }

    auto method = nyx::sdk::engine::TryFindMethod(
        "Assembly-CSharp.dll::Game.Player::TakeDamage(1)"
    );
    if (!method.ok()) {
        NYX_LOGW("method lookup failed: %s", method.result.detail.c_str());
        return;
    }

    void* entry = nyx::sdk::engine::MethodPtr(method.value);
    NYX_LOGI("method=%s entry=%p", method.value.name.c_str(), entry);
}
```

签名校验绑定示例：

```cpp
const char* params[] = {"System.Int32"};

nyx::sdk::engine::MethodSignature sig;
sig.image_name = "Assembly-CSharp.dll";
sig.class_namespace = "Game";
sig.class_name = "Player";
sig.name = "TakeDamage";
sig.return_type = "System.Void";
sig.params = params;
sig.param_count = 1;

nyx::sdk::engine::Method method;
auto result = nyx::sdk::engine::BindMethod(sig, &method);
if (!result.ok()) {
    NYX_LOGW("bind method failed: %s", result.detail.c_str());
}
```

### `stack.h`：调用栈诊断

`stack::Capture()` 会返回原始 PC、归一化 PC、模块路径、符号、Hook frame 标注和恢复状态，适合在 Hook replacement、异常路径或 doctor 中输出诊断。

```cpp
#include "sdk/include/stack.h"
#include "sdk/include/utils.h"

void DumpStack() {
    auto frames = nyx::sdk::stack::TryCapture(32);
    if (!frames.ok()) {
        NYX_LOGW("stack capture failed: %s", frames.result.detail.c_str());
        return;
    }

    for (const auto& frame : frames.value) {
        NYX_LOGI(
            "pc=%p module=%s symbol=%s",
            reinterpret_cast<void*>(frame.normalized_pc),
            frame.module_path.c_str(),
            frame.symbol.c_str()
        );
    }
}
```

### `test.h`：Doctor、集成门禁和 Benchmark

`test.h` 受编译开关控制：

| 宏 | 含义 |
| --- | --- |
| `NYX_ENABLE_NATIVE_TESTS` | 打开 `CheckEnv()`、`CheckCrypto()`、`CheckRuntime()` 等 native doctor。 |
| `NYX_ENABLE_INTEGRATION_GATES` | 打开 `CheckRelease()` 等集成门禁。 |
| `NYX_ENABLE_BENCHMARKS` | 打开 `BENCHMARK(name)` 作用域计时。 |

Release 构建会关闭这些测试和 mock auth。

```cpp
#include "sdk/include/test.h"
#include "sdk/include/utils.h"

void RunDoctor() {
#if NYX_ENABLE_NATIVE_TESTS
    const bool ok =
        nyx::sdk::test::CheckEnv() &&
        nyx::sdk::test::CheckCrypto() &&
        nyx::sdk::test::CheckRuntime();
    NYX_LOGI("native doctor=%d", ok ? 1 : 0);
#endif
}

void ProfileWork() {
    BENCHMARK("example work");
    // 这里放需要计时的逻辑。
}
```

## 工程结构

- `app/build.gradle.kts`：Android 构建入口，默认 ABI 为 `arm64-v8a` / `armeabi-v7a`。
- `app/src/main/cpp/CMakeLists.txt`：Native 构建入口，组织 SDK、Runtime、Engine、Overlay、Vendor 和 Demo。
- `app/src/main/cpp/sdk/include/`：MOD 可依赖的唯一公共 SDK 面。
- `app/src/main/cpp/src/`：NyxCore 内部实现，业务模块不要直接 include。
- `app/src/main/cpp/mods/demo/src/main.cpp`：默认公开 Demo MOD。
- `app/src/main/kotlin/dev/nyxcore/manager/`：Android Activity、Native bridge 和 GL surface 容器。
- `app/src/main/cpp/vendor/`：三方依赖快照；版本、来源和 License 以 `vendor/README.md` 为准。

## 构建环境

- Gradle Wrapper：`gradle-8.13-bin.zip`
- Android Gradle Plugin：`8.13.2`
- Kotlin Gradle Plugin：`2.0.21`
- JDK：建议使用 Android Studio bundled JDK 17；Java / Kotlin 编译目标为 11。
- Android SDK：`compileSdk = 36`，`minSdk = 24`，`targetSdk = 36`。
- NDK：当前固定为 `30.0.14904198-beta1`。
- CMake：`cmake_minimum_required(VERSION 3.22.1)`。

NDK 查找优先级为：`-PnyxNdkPath=<path>`、`local.properties` 中的 `nyx.ndk.dir`、`local.properties` 中的 `ndk.dir`、`ANDROID_NDK_HOME`、`ANDROID_NDK_ROOT`。Windows 路径建议使用正斜杠，例如：

```properties
sdk.dir=C:/Users/<you>/AppData/Local/Android/Sdk
nyx.ndk.dir=D:/Android/android-ndk-r30-beta1-windows
```

当前 Release 构建默认启用 ALLVM `-mllvm` 参数。请使用 [ALLVM v1.2.0](https://github.com/abcdefgjh-li/ALLVM/releases/tag/v1.2.0) 提供的 `android-ndk-r30-beta1-windows.zip`；普通官方 NDK 可用于 Debug 验证，但 Release 很可能因不认识 ALLVM 参数而失败。

Release 签名是可选配置。构建脚本会优先读取 `local.properties`、Gradle 参数或环境变量中的 `nyx.release.signing.*` / `NYX_RELEASE_*`；如果你需要本地测试签名，可以自己创建 `keystores/public-signing.properties` 和对应 keystore，但不要把真实签名文件或密码提交到公开仓库。

## 常用命令

```powershell
.\gradlew.bat :app:assembleDebug
.\gradlew.bat :app:testDebugUnitTest
.\gradlew.bat :app:assembleRelease
.\gradlew.bat :app:connectedDebugAndroidTest
.\gradlew.bat :app:nyxInfo
.\gradlew.bat :app:nyxMods
```

- `:app:assembleDebug`：构建 Debug APK；默认打开 native tests、integration gates 和 benchmarks。
- `:app:testDebugUnitTest`：运行 JVM 单元测试，不需要连接 Android 设备。
- `:app:assembleRelease`：构建 Release APK；Release 会关闭 native tests、integration gates、benchmarks 和 mock auth。
- `:app:connectedDebugAndroidTest`：运行真机 instrumentation gate，需要已连接并授权的 Android 设备。
- `:app:nyxInfo`：打印当前 active MOD、输出 SO 名称、NDK 路径和一次性切换命令。
- `:app:nyxMods`：列出 `app/src/main/cpp/mods/` 下可用的 MOD。

## 切换 MOD

临时构建某个 MOD 时不用改文件，直接传 Gradle 参数：

```powershell
.\gradlew.bat :app:assembleDebug -PnyxMod=demo
```

Gradle 会把 `nyxMod` 转成 CMake 的 `-DNYX_TARGET_TAG=<mod>`，并同步切换 `BuildConfig.NYX_ACTIVE_MOD`、`BuildConfig.NYX_NATIVE_LIBRARY`、native 源码、assets 目录和最终 `lib<outputName>.so`。

MOD 选择优先级：

1. `-PnyxMod=<mod>`：临时覆盖，适合本次命令快速切换。
2. `config/active.properties` 的 `mod=<mod>`：不传 `-PnyxMod` 时的项目默认值。
3. CMake 内部默认 `demo`：只在绕过 Gradle、直接调用 CMake 且没有传 `-DNYX_TARGET_TAG` 时兜底。

带 `publish=false` 的私有 MOD 可以 Debug 构建，但会阻止普通 Release / assemble / bundle 任务，避免误发私有产物。确认要发布时再把对应 `mod.properties` 改成 `publish=true`。

## 使用边界

- NyxCore 的 Runtime / Engine 能力仅用于自有 App、自有 Demo、授权测试环境和明确授权的 MOD 场景。
- MOD 业务层只能依赖 `sdk/include/` 公开契约；Dobby、ByteHook、ShadowHook、ImGui、IL2CPP 原始 API 均为内部实现细节，不建议直接依赖。
- 已进入 Runtime / Engine 主线的能力按 SDK 契约推进；未完成包装的功能会通过状态返回和 doctor 进行跟踪与反馈。
- 默认 Demo 可以不主动触发所有底层路径，但 SDK 和诊断会如实表达当前能力状态。
- 三方依赖版本、来源和 License 统一维护在 `app/src/main/cpp/vendor/README.md`。
## 仓库内容说明

本仓库已作为正式开源项目对外发布，以下内容说明仓库的组织方式与公开范围：

- 默认 active mod 为 `demo`：`config/active.properties` 中保持 `mod=demo`。
- `app/src/main/cpp/mods/demo/` 可独立构建，是公开示例的默认入口。
- 私有 MOD 已从 Git 索引移除，并通过 `.gitignore` 排除，不纳入公开分支。
- 仓库中不包含 `local.properties`、真实 keystore、签名密码、私钥、证书、token、API key 或授权 profile。
- 仓库中不包含 payload、加密资源、私有资源包、真实偏移、服务端字段映射或调试日志。
- 仓库中不包含无法确认授权来源的第三方素材或二进制载荷。
- 新增公共能力时，请继续通过 `sdk/include/` 暴露，避免将内部实现细节带到公开层。

### 提交前检查建议

贡献者在提交 PR 前，建议运行以下命令进行自查：

```powershell
git status --short
git status --ignored --short
git ls-files app/src/main/cpp/mods
git ls-files local.properties keystores
rg -n "^(apiToken|appKey|rc4Key|storePassword|keyPassword)=\S+" -S --glob "*.properties" --glob "!local.properties.example" --glob "!keystores/**" --glob "!app/src/main/cpp/vendor/**"
rg -n -S --glob "!keystores/**" --glob "!app/src/main/cpp/vendor/**" -- "-----BEGIN [A-Z ]*PRIVATE KEY-----"
```
## 贡献

- 开发环境：Android Studio 或等价 Gradle/JDK 环境、JDK 17、Android SDK 36、CMake 3.22.1+；Debug 可用普通 NDK，Release 需要 ALLVM-patched Android NDK r30 beta1。
- 推荐流程：先跑 `.\gradlew.bat :app:nyxInfo`、`.\gradlew.bat :app:nyxMods`、`.\gradlew.bat :app:assembleDebug -PnyxMod=demo`、`.\gradlew.bat :app:testDebugUnitTest`。
- 代码边界：公开 MOD 只依赖 `sdk/include/`，不要直接 include `src/`、`vendor/` 或具体 Hook / ImGui / IL2CPP 后端头文件。
- 提交前检查：改动涉及 Native Runtime 或真机能力时，再补 `.\gradlew.bat :app:connectedDebugAndroidTest -PnyxMod=demo`，并确认没有把真实授权 profile、API token、签名文件、私钥、payload、偏移或私有资源提交进去。
- 文档风格：面向第一次下载项目的人写清楚目的、入口、构建方式和边界；示例优先展示 `sdk/include/` 公开契约。

## 安全与贡献规范

本项目遵循以下安全与贡献规范，确保社区健康发展：

- 本仓库提供 `mods/demo` 示例模块、`sdk/include/` 公共 SDK 契约以及 Android 示例 App 的完整构建路径，欢迎在此基础上进行扩展与贡献。
- 请勿将第三方应用、游戏、服务端或非授权目标作为测试对象提交 Issue 或复现材料。
- `local.properties`、`keystores/` 中的真实签名文件、`secure/` 目录以及 `*.local.properties`、`*.secret.properties` 等敏感配置文件不应提交到仓库。
- 如发现凭据、签名文件、私钥、token、授权 profile、私有 MOD、payload 或敏感配置泄露，请通过安全渠道私下报告维护者，邮箱ammer369@gmail.com，协助修复后再决定是否需要公开说明。
- 如需使用测试签名，请使用专门的无价值 Demo key，并在文档中明确标注不可用于正式发布。
## 许可证

- 本项目根目录下的 `LICENSE` 文件已确定主许可证，请查阅以了解具体条款。
- 三方依赖继续遵循各自目录下的许可证，所有许可证汇总信息以 `app/src/main/cpp/vendor/README.md` 为准。
