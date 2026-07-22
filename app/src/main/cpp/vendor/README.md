# Vendor Dependency Manifest

This directory stores pinned source snapshots for third-party native
dependencies used by NyxCore. Do not replace these folders with a floating
`latest` download. Every update must keep the upstream license and record the
new tag or commit here.

| Library | Pinned Version | Commit | Source | License | Local Path | Link Mode | Enters Final SO |
| --- | --- | --- | --- | --- | --- | --- | --- |
| nlohmann_json | `v3.12.0` | `55f93686c01528224f448c19128836e7df245f72` | `https://github.com/nlohmann/json/tree/v3.12.0` | MIT | `nlohmann_json/` | `INTERFACE` | Only if a linked target includes it |
| cpp-httplib | `v0.49.0` | `2132205e1a69c9fce8096f085b1b8d72efc759fa` | `https://github.com/yhirose/cpp-httplib/tree/v0.49.0` | MIT | `cpp-httplib/` | `INTERFACE` | Only if a linked target includes it |
| Monocypher | `4.0.3` | `ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` | `https://monocypher.org/download/monocypher-4.0.3.tar.gz` | BSD-2-Clause | `monocypher/` | `STATIC` | Yes, when linked |
| Mbed TLS | `v3.6.7` | `068ff080b369adfac81509f9b57b2afabaf82dc5` | `https://github.com/Mbed-TLS/mbedtls/tree/v3.6.7` | Apache-2.0 OR GPL-2.0-or-later | `mbedtls/` | `STATIC` | Yes, when linked |
| Dear ImGui | `v1.92.8` | `8936b58fe26e8c3da834b8f60b06511d537b4c63` | `https://github.com/ocornut/imgui/tree/v1.92.8` | MIT | `imgui/` | `STATIC` | Yes, when linked |
| Dobby | Mixed local prebuilt snapshot | Header/ARM build `c343f74888dffad84d9ad08d9c433456`; x86/x86_64 archives from LGL package built from `0932d69c320e786672361ab53825ba8f4245e9d3`; upstream `latest` license from `5dfc8546954ce3b3198132ab13fddb89ee92cdd7` | Local ARM package moved from `E:\project\NyxCore\dobby`; x86/x86_64 package from `third_party/github-references/LGLTeam_Android-Mod-Menu/app/src/main/jni/Dobby`; license source `https://github.com/jmpews/Dobby/tree/latest` | Apache-2.0 | `dobby/` | `INTERFACE` plus ABI prebuilt archive when available | Yes, when linked on supported ABIs |
| xDL | `2.3.0` from LGL snapshot | LGL reference commit `bdc5184b48c12cccb9cbe1c6853d3c6eebd6256d` | `third_party/github-references/LGLTeam_Android-Mod-Menu/app/src/main/jni/xDL`; upstream `https://github.com/hexhacking/xDL` | MIT | `xdl/` | `STATIC` target, not linked by default | Only if explicitly linked; avoid linking alongside static ShadowHook xDL symbols |
| KittyMemory | Local source snapshot with bundled Keystone archives | LGL reference commit `bdc5184b48c12cccb9cbe1c6853d3c6eebd6256d`; KittyMemory license from `https://github.com/MJx0/KittyMemory` | `third_party/github-references/LGLTeam_Android-Mod-Menu/app/src/main/jni/KittyMemory`; upstream `https://github.com/MJx0/KittyMemory` | MIT; bundled Keystone archive is GPL-2.0 | `kittymemory/` | `STATIC` target plus ABI Keystone archive, not linked by default | Only if explicitly linked |
| ShadowHook | Local source snapshot | Local snapshot from `third_party/github-references/ShadowHook_android-inline-hook` | `https://github.com/bytedance/android-inline-hook` | MIT | `shadowhook/` | `STATIC` source build for ARM/ARM64 ByteHook support | Yes, when linked on ARM/ARM64 ByteHook builds |
| ByteHook | `v1.1.2` | `a8bd254f6e53022b65136f40d10ae3763b6ef8ad` | `https://github.com/bytedance/bhook/tree/v1.1.2` | MIT | `bytehook/` | `STATIC` source build for PLT hook backend | Yes, when linked on supported ABIs |
| AlguiMemTool reference | `local-2026-07-13` | Local source reference only | `D:\Project\app\src\main\jni\AlguiMemTool.h` | MIT | Reference note only; not tracked in repo | Reference only, not included by CMake | No |

## Contents

- `nlohmann_json/` keeps the single public header and upstream MIT license.
- `cpp-httplib/` keeps the single public header and upstream MIT license.
- `monocypher/` keeps the upstream `src/` folder and license.
- `mbedtls/` keeps the upstream source folders needed for later CMake target
  wiring. NyxCore-specific trimming must live outside this folder.
- `imgui/` keeps the upstream source snapshot for overlay/debug UI work.
- `dobby/` keeps the supplied header, ARM prebuilt archives, x86/x86_64
  archives copied from the LGL Android-Mod-Menu reference package, and upstream
  Apache-2.0 license. Replace it with one pinned source build before production
  hook work.
- `xdl/` keeps the xDL source snapshot copied from the LGL Android-Mod-Menu
  reference package. It is available as `vendor_xdl`, but is not linked into
  `demo` by default because ShadowHook also carries an internal xDL copy.
- `kittymemory/` keeps the KittyMemory source snapshot and bundled Keystone
  Android static archives copied from the LGL Android-Mod-Menu reference
  package. It is available as `vendor_kittymemory`, but is not linked into
  `demo` by default because it pulls in the Keystone archive and memory patch
  helpers only needed by explicit users.
- `shadowhook/` keeps the upstream native source snapshot and MIT license. It is
  built only for `armeabi-v7a` and `arm64-v8a`, where ByteHook depends on its
  linker init/fini callback support.
- `bytehook/` keeps the upstream native core snapshot, public header, bundled
  third-party LSS/queue headers, and MIT license. NyxCore builds it for
  x86/x86_64 directly and for ARM/ARM64 through the vendored ShadowHook target.
- `AlguiMemTool` is a reference-only migration source recorded for auditability.
  The original header is not copied into `vendor/`, is not included from CMake,
  and only informs the MemorySDK-shaped implementation in `src/runtime/memory/`.

## Update Rules

1. Pin a tag or immutable commit before replacing a folder.
2. Preserve the upstream license file verbatim.
3. Re-run the Android native build after every update.
4. For Mbed TLS, update the source whitelist and custom config together.
