cmake_minimum_required(VERSION 3.22.1)

# NyxCore native 根工程：这里只组织 target，具体开关和配置读取放到 cmake/ 下
project(nyxcore_native)
enable_language(ASM)

# 全工程默认 C11 / C++20，具体 target 还会在 security.cmake 里再补一层属性
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cmake/ 下按职责分层：框架安全默认值、通用 properties 工具、鉴权配置和 mod 配置分开维护
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/security.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/properties.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/auth.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/mods.cmake")

# 项目路径：第三方库集中放 vendor，mbedTLS 使用项目内裁剪配置
set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(MBEDTLS_DIR "${VENDOR_DIR}/mbedtls")
set(XDL_DIR "${VENDOR_DIR}/xdl")
set(KITTYMEMORY_DIR "${VENDOR_DIR}/kittymemory")
set(NYX_MBEDTLS_CONFIG "src/crypto/config/mbedtls_config.h")

# Android 平台库：log/dl 多个 runtime 和 hook 后端都会用到
find_library(log-lib log)
find_library(dl-lib dl)

# 构建开关和测试探针默认值统一在 security.cmake 里维护
nyx_configure_build_switches()
nyx_configure_test_probe_macros()

# Header-only JSON：只传 include，不产生实际 native 产物
add_library(vendor_json INTERFACE)
target_include_directories(
    vendor_json
    SYSTEM INTERFACE
    "${VENDOR_DIR}/nlohmann_json/single_include"
)
nyx_apply_native_defaults(vendor_json)

# cpp-httplib 以 mbedTLS 后端编译，最终链接到项目裁剪后的 mbedTLS
add_library(vendor_httplib INTERFACE)
target_include_directories(
    vendor_httplib
    SYSTEM INTERFACE
    "${VENDOR_DIR}/cpp-httplib"
)
target_compile_definitions(
    vendor_httplib
    INTERFACE
    CPPHTTPLIB_MBEDTLS_SUPPORT
)
nyx_apply_native_defaults(vendor_httplib)

# Monocypher 只编进需要的 C 源码，作为轻量加密基础库
add_library(
    vendor_monocypher
    STATIC
    EXCLUDE_FROM_ALL
    "${VENDOR_DIR}/monocypher/src/monocypher.c"
    "${VENDOR_DIR}/monocypher/src/optional/monocypher-ed25519.c"
)
target_include_directories(
    vendor_monocypher
    PUBLIC
    "${VENDOR_DIR}/monocypher/src"
    "${VENDOR_DIR}/monocypher/src/optional"
)
nyx_apply_native_defaults(vendor_monocypher)

# ImGui 只保留 Android + OpenGL3 后端，避免把示例和其它平台后端带进来
add_library(
    vendor_imgui
    STATIC
    EXCLUDE_FROM_ALL
    "${VENDOR_DIR}/imgui/imgui.cpp"
    "${VENDOR_DIR}/imgui/imgui_draw.cpp"
    "${VENDOR_DIR}/imgui/imgui_tables.cpp"
    "${VENDOR_DIR}/imgui/imgui_widgets.cpp"
    "${VENDOR_DIR}/imgui/backends/imgui_impl_android.cpp"
    "${VENDOR_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(
    vendor_imgui
    SYSTEM PUBLIC
    "${VENDOR_DIR}/imgui"
    "${VENDOR_DIR}/imgui/backends"
)
nyx_apply_native_defaults(vendor_imgui)

# Dobby 使用预编译 ABI 静态库；缺少当前 ABI 时直接失败
add_library(vendor_dobby INTERFACE)
target_include_directories(
    vendor_dobby
    SYSTEM INTERFACE
    "${VENDOR_DIR}/dobby"
)
if(NOT ANDROID)
    message(FATAL_ERROR "Dobby inline hook backend requires an Android ABI")
endif()
set(DOBBY_ARCHIVE "${VENDOR_DIR}/dobby/libraries/${ANDROID_ABI}/libdobby.a")
if(NOT EXISTS "${DOBBY_ARCHIVE}")
    message(FATAL_ERROR "Dobby is required for ${ANDROID_ABI}: missing ${DOBBY_ARCHIVE}")
endif()
target_link_libraries(vendor_dobby INTERFACE "${DOBBY_ARCHIVE}")
nyx_apply_native_defaults(vendor_dobby)

# xDL 是可选 vendor：存在源码就编静态库，不存在就保留空 INTERFACE target
set(NYX_XDL_VENDOR_PRESENT OFF)
if(ANDROID AND EXISTS "${XDL_DIR}/xdl.h" AND EXISTS "${XDL_DIR}/xdl.c")
    set(NYX_XDL_VENDOR_PRESENT ON)
endif()
if(NYX_XDL_VENDOR_PRESENT)
    set(XDL_SOURCES
        "${XDL_DIR}/xdl.c"
        "${XDL_DIR}/xdl_iterate.c"
        "${XDL_DIR}/xdl_linker.c"
        "${XDL_DIR}/xdl_lzma.c"
        "${XDL_DIR}/xdl_util.c"
    )
    add_library(
        vendor_xdl
        STATIC
        EXCLUDE_FROM_ALL
        ${XDL_SOURCES}
    )
    target_include_directories(
        vendor_xdl
        SYSTEM PUBLIC
        "${XDL_DIR}"
    )
    target_compile_features(vendor_xdl PRIVATE c_std_11)
    target_link_libraries(vendor_xdl PUBLIC ${dl-lib})
else()
    add_library(vendor_xdl INTERFACE)
endif()
nyx_apply_native_defaults(vendor_xdl)

# KittyMemory 依赖 Keystone 预编译库；vendor 不完整时降级为空 target
set(NYX_KITTYMEMORY_VENDOR_PRESENT OFF)
if(ANDROID AND EXISTS "${KITTYMEMORY_DIR}/KittyMemory.hpp" AND EXISTS "${KITTYMEMORY_DIR}/KittyMemory.cpp")
    set(NYX_KITTYMEMORY_VENDOR_PRESENT ON)
endif()
if(NYX_KITTYMEMORY_VENDOR_PRESENT)
    set(KITTYMEMORY_KEYSTONE_ARCHIVE "${KITTYMEMORY_DIR}/Deps/Keystone/libs-android/${ANDROID_ABI}/libkeystone.a")
    if(NOT EXISTS "${KITTYMEMORY_KEYSTONE_ARCHIVE}")
        message(FATAL_ERROR "KittyMemory Keystone archive is required for ${ANDROID_ABI}: missing ${KITTYMEMORY_KEYSTONE_ARCHIVE}")
    endif()

    set(KITTYMEMORY_SOURCES
        "${KITTYMEMORY_DIR}/KittyAsm.cpp"
        "${KITTYMEMORY_DIR}/KittyIOFile.cpp"
        "${KITTYMEMORY_DIR}/KittyMemory.cpp"
        "${KITTYMEMORY_DIR}/KittyPtrValidator.cpp"
        "${KITTYMEMORY_DIR}/KittyScanner.cpp"
        "${KITTYMEMORY_DIR}/KittyUtils.cpp"
        "${KITTYMEMORY_DIR}/MemoryBackup.cpp"
        "${KITTYMEMORY_DIR}/MemoryPatch.cpp"
    )
    add_library(
        vendor_kittymemory
        STATIC
        EXCLUDE_FROM_ALL
        ${KITTYMEMORY_SOURCES}
    )
    target_include_directories(
        vendor_kittymemory
        SYSTEM PUBLIC
        "${KITTYMEMORY_DIR}"
        "${KITTYMEMORY_DIR}/Deps/Keystone/includes"
    )
    target_compile_features(vendor_kittymemory PRIVATE cxx_std_17)
    target_link_libraries(
        vendor_kittymemory
        PUBLIC
        "${KITTYMEMORY_KEYSTONE_ARCHIVE}"
        ${log-lib}
        ${dl-lib}
    )
else()
    add_library(vendor_kittymemory INTERFACE)
endif()
nyx_apply_native_defaults(vendor_kittymemory)

# hook 后端路径：bytehook 可能复用 shadowhook 的 linker 能力
set(BYTEHOOK_INCLUDE_DIR "${VENDOR_DIR}/bytehook/include")
set(BYTEHOOK_SOURCE_DIR "${VENDOR_DIR}/bytehook/source")
set(SHADOWHOOK_SOURCE_DIR "${VENDOR_DIR}/shadowhook/source")

# shadowhook 只支持 ARM/ARM64；Debug 保留独立 so，Release 静态合入主库
set(NYX_SHADOWHOOK_VENDOR_PRESENT OFF)
set(NYX_SHADOWHOOK_BUILD_SHARED OFF)
if(ANDROID AND (ANDROID_ABI STREQUAL "armeabi-v7a" OR ANDROID_ABI STREQUAL "arm64-v8a"))
    if(EXISTS "${SHADOWHOOK_SOURCE_DIR}/include/shadowhook.h" AND EXISTS "${SHADOWHOOK_SOURCE_DIR}/shadowhook.c")
        set(NYX_SHADOWHOOK_VENDOR_PRESENT ON)
    endif()
endif()
if(NYX_SHADOWHOOK_VENDOR_PRESENT AND NOT NYX_RELEASE_BUILD)
    set(NYX_SHADOWHOOK_BUILD_SHARED ON)
endif()
if(NYX_SHADOWHOOK_VENDOR_PRESENT)
    if(NYX_SHADOWHOOK_BUILD_SHARED)
        set(NYX_SHADOWHOOK_LIBRARY_KIND SHARED)
    else()
        set(NYX_SHADOWHOOK_LIBRARY_KIND STATIC)
    endif()

    # shadowhook 的汇编源需要按 ABI 指定 clang --target
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(SHADOWHOOK_ARCH "arm64")
        set(SHADOWHOOK_ASM_TARGET "aarch64-none-linux-android${CMAKE_SYSTEM_VERSION}")
        set(SHADOWHOOK_ARCH_SOURCES
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm64/sh_a64.c"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm64/sh_glue.S"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm64/sh_inst.c"
        )
    else()
        set(SHADOWHOOK_ARCH "arm")
        set(SHADOWHOOK_ASM_TARGET "armv7-none-linux-androideabi${CMAKE_SYSTEM_VERSION}")
        set(SHADOWHOOK_ARCH_SOURCES
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_a32.c"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_glue.S"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_inst.c"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_t16.c"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_t32.c"
            "${SHADOWHOOK_SOURCE_DIR}/arch/arm/sh_txx.c"
        )
    endif()

    # shadowhook 源码不走 add_subdirectory，避免它自己的示例/测试配置影响主工程
    set(SHADOWHOOK_SOURCES
        "${SHADOWHOOK_SOURCE_DIR}/shadowhook.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_elf.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_enter.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_hub.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_island.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_linker.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_recorder.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_safe.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_switch.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_task.c"
        "${SHADOWHOOK_SOURCE_DIR}/sh_xdl.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/bytesig.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/sh_errno.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/sh_log.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/sh_ref.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/sh_trampo.c"
        "${SHADOWHOOK_SOURCE_DIR}/common/sh_util.c"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl/xdl.c"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl/xdl_iterate.c"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl/xdl_linker.c"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl/xdl_lzma.c"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl/xdl_util.c"
        ${SHADOWHOOK_ARCH_SOURCES}
    )
    add_library(
        vendor_shadowhook
        ${NYX_SHADOWHOOK_LIBRARY_KIND}
        EXCLUDE_FROM_ALL
        ${SHADOWHOOK_SOURCES}
    )
    set_target_properties(
        vendor_shadowhook
        PROPERTIES
        OUTPUT_NAME "shadowhook"
        # shadowhook 自己有公开 C API，不能套项目默认 hidden visibility
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
        POSITION_INDEPENDENT_CODE ON
    )
    target_include_directories(
        vendor_shadowhook
        SYSTEM PUBLIC
        "${SHADOWHOOK_SOURCE_DIR}/include"
        PRIVATE
        "${SHADOWHOOK_SOURCE_DIR}"
        "${SHADOWHOOK_SOURCE_DIR}/arch/${SHADOWHOOK_ARCH}"
        "${SHADOWHOOK_SOURCE_DIR}/common"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/xdl"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/bsd"
        "${SHADOWHOOK_SOURCE_DIR}/third_party/lss"
    )
    target_compile_features(vendor_shadowhook PRIVATE c_std_11)
    target_compile_options(
        vendor_shadowhook
        PRIVATE
        -Os
        -ffunction-sections
        -fdata-sections
        "$<$<COMPILE_LANGUAGE:ASM>:--target=${SHADOWHOOK_ASM_TARGET}>"
        "$<$<COMPILE_LANGUAGE:ASM>:--sysroot=${CMAKE_SYSROOT}>"
    )
    if(NYX_SHADOWHOOK_BUILD_SHARED)
        # Debug 下独立 so 需要 version script，导出面按 shadowhook 自带规则控制
        target_link_options(
            vendor_shadowhook
            PRIVATE
            "$<$<STREQUAL:${ANDROID_ABI},arm64-v8a>:-Wl,-z,max-page-size=16384>"
            -Os
            "-Wl,--exclude-libs,ALL"
            "-Wl,--gc-sections"
            "-Wl,--undefined-version"
            "-Wl,--version-script=${SHADOWHOOK_SOURCE_DIR}/shadowhook.map.txt"
        )
    endif()
    target_link_libraries(vendor_shadowhook PUBLIC ${log-lib} ${dl-lib})
    if(NYX_SHADOWHOOK_BUILD_SHARED)
        # shadowhook_nothing 是配套占位 so，使用 tiny 默认值压到最小
        add_library(
            vendor_shadowhook_nothing
            SHARED
            EXCLUDE_FROM_ALL
            "${SHADOWHOOK_SOURCE_DIR}/nothing/sh_nothing.c"
        )
        set_target_properties(
            vendor_shadowhook_nothing
            PROPERTIES
            OUTPUT_NAME "shadowhook_nothing"
        )
        nyx_apply_tiny_shared_defaults(vendor_shadowhook_nothing)
    endif()
else()
    add_library(vendor_shadowhook INTERFACE)
    nyx_apply_native_defaults(vendor_shadowhook)
endif()
if(TARGET vendor_shadowhook_nothing)
    nyx_apply_native_defaults(vendor_shadowhook_nothing)
endif()

# bytehook 按 ABI 和源码存在性启用；没有可用实现时保持空 target 方便主库照常链接
set(NYX_BYTEHOOK_VENDOR_PRESENT OFF)
set(NYX_BYTEHOOK_ABI_SUPPORTED OFF)
set(NYX_PLT_HOOK_HAS_BYTEHOOK 0)
set(NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER 0)
if(ANDROID)
    if(ANDROID_ABI STREQUAL "x86" OR ANDROID_ABI STREQUAL "x86_64")
        set(NYX_BYTEHOOK_ABI_SUPPORTED ON)
    elseif(NYX_SHADOWHOOK_VENDOR_PRESENT)
        # ARM/ARM64 的 bytehook 依赖 shadowhook vendor 已经存在
        if(ANDROID_ABI STREQUAL "armeabi-v7a" OR ANDROID_ABI STREQUAL "arm64-v8a")
            set(NYX_BYTEHOOK_ABI_SUPPORTED ON)
        endif()
    endif()
endif()
if(NYX_BYTEHOOK_ABI_SUPPORTED)
    if(EXISTS "${BYTEHOOK_INCLUDE_DIR}/bytehook.h" AND EXISTS "${BYTEHOOK_SOURCE_DIR}/bytehook.c")
        set(NYX_BYTEHOOK_VENDOR_PRESENT ON)
    endif()
endif()
if(NYX_BYTEHOOK_VENDOR_PRESENT)
    set(BYTEHOOK_SOURCES
        "${BYTEHOOK_SOURCE_DIR}/bh_array.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_cfi.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_dl.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_dl_iterate.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_dl_monitor.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_elf.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_elf_manager.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_elf_relocator.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_hub.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_linker.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_linker_mutex.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_log.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_recorder.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_safe.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_sleb128.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_switch.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_task.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_task_manager.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_trampo.c"
        "${BYTEHOOK_SOURCE_DIR}/bh_util.c"
        "${BYTEHOOK_SOURCE_DIR}/bytehook.c"
        "${BYTEHOOK_SOURCE_DIR}/bytesig.c"
    )
    add_library(
        vendor_bytehook
        STATIC
        EXCLUDE_FROM_ALL
        ${BYTEHOOK_SOURCES}
    )
    target_include_directories(
        vendor_bytehook
        SYSTEM PUBLIC
        "${BYTEHOOK_INCLUDE_DIR}"
        "${BYTEHOOK_SOURCE_DIR}"
        "${BYTEHOOK_SOURCE_DIR}/third_party/bsd"
        "${BYTEHOOK_SOURCE_DIR}/third_party/lss"
    )
    target_compile_features(vendor_bytehook PRIVATE c_std_11)
    target_link_libraries(vendor_bytehook PUBLIC ${log-lib} ${dl-lib})
    set(NYX_PLT_HOOK_HAS_BYTEHOOK 1)
    if(TARGET vendor_shadowhook)
        target_link_libraries(vendor_bytehook PUBLIC vendor_shadowhook)
    endif()
    if(NYX_SHADOWHOOK_BUILD_SHARED)
        set(NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER 1)
    endif()
else()
    add_library(vendor_bytehook INTERFACE)
endif()
nyx_apply_native_defaults(vendor_bytehook)

# mbedTLS 手动列源：只编项目当前需要的 TLS/哈希/签名能力
set(MBEDTLS_SOURCES
    "${MBEDTLS_DIR}/library/aes.c"
    "${MBEDTLS_DIR}/library/asn1parse.c"
    "${MBEDTLS_DIR}/library/asn1write.c"
    "${MBEDTLS_DIR}/library/base64.c"
    "${MBEDTLS_DIR}/library/bignum.c"
    "${MBEDTLS_DIR}/library/bignum_core.c"
    "${MBEDTLS_DIR}/library/bignum_mod.c"
    "${MBEDTLS_DIR}/library/bignum_mod_raw.c"
    "${MBEDTLS_DIR}/library/cipher.c"
    "${MBEDTLS_DIR}/library/cipher_wrap.c"
    "${MBEDTLS_DIR}/library/constant_time.c"
    "${MBEDTLS_DIR}/library/ctr_drbg.c"
    "${MBEDTLS_DIR}/library/ecdh.c"
    "${MBEDTLS_DIR}/library/ecdsa.c"
    "${MBEDTLS_DIR}/library/ecp.c"
    "${MBEDTLS_DIR}/library/ecp_curves.c"
    "${MBEDTLS_DIR}/library/entropy.c"
    "${MBEDTLS_DIR}/library/entropy_poll.c"
    "${MBEDTLS_DIR}/library/error.c"
    "${MBEDTLS_DIR}/library/gcm.c"
    "${MBEDTLS_DIR}/library/hkdf.c"
    "${MBEDTLS_DIR}/library/md.c"
    "${MBEDTLS_DIR}/library/md5.c"
    "${MBEDTLS_DIR}/library/oid.c"
    "${MBEDTLS_DIR}/library/pem.c"
    "${MBEDTLS_DIR}/library/pk.c"
    "${MBEDTLS_DIR}/library/pk_ecc.c"
    "${MBEDTLS_DIR}/library/pk_wrap.c"
    "${MBEDTLS_DIR}/library/pkparse.c"
    "${MBEDTLS_DIR}/library/platform.c"
    "${MBEDTLS_DIR}/library/platform_util.c"
    "${MBEDTLS_DIR}/library/rsa.c"
    "${MBEDTLS_DIR}/library/rsa_alt_helpers.c"
    "${MBEDTLS_DIR}/library/sha1.c"
    "${MBEDTLS_DIR}/library/sha256.c"
    "${MBEDTLS_DIR}/library/sha512.c"
    "${MBEDTLS_DIR}/library/ssl_ciphersuites.c"
    "${MBEDTLS_DIR}/library/ssl_client.c"
    "${MBEDTLS_DIR}/library/ssl_msg.c"
    "${MBEDTLS_DIR}/library/ssl_tls.c"
    "${MBEDTLS_DIR}/library/ssl_tls12_client.c"
    "${MBEDTLS_DIR}/library/version.c"
    "${MBEDTLS_DIR}/library/version_features.c"
    "${MBEDTLS_DIR}/library/x509.c"
    "${MBEDTLS_DIR}/library/x509_crt.c"
)

add_library(
    vendor_mbedtls
    STATIC
    EXCLUDE_FROM_ALL
    ${MBEDTLS_SOURCES}
)
target_include_directories(
    vendor_mbedtls
    PUBLIC
    "${MBEDTLS_DIR}/include"
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${MBEDTLS_DIR}/library"
)
target_compile_definitions(
    vendor_mbedtls
    PUBLIC
    MBEDTLS_CONFIG_FILE="${NYX_MBEDTLS_CONFIG}"
)
nyx_apply_native_defaults(vendor_mbedtls)

# 可选源码组：Release 默认关闭测试、闸门、benchmark 和 mock provider
set(NYX_AUTH_MOCK_SRC)
if(NYX_AUTH_ENABLE_MOCK)
    list(APPEND NYX_AUTH_MOCK_SRC "src/core/auth/providers/mock/mock_provider.cpp")
endif()

set(NYX_BENCHMARK_SRC)
if(NYX_ENABLE_BENCHMARKS)
    list(APPEND NYX_BENCHMARK_SRC
        "src/tests/support/time_profiler.cpp"
    )
endif()

set(NYX_NATIVE_TEST_SRC)
if(NYX_ENABLE_NATIVE_TESTS)
    list(APPEND NYX_NATIVE_TEST_SRC
        "src/tests/suites/environment.cpp"
        "src/tests/suites/string.cpp"
        "src/tests/suites/crypto_vectors.cpp"
        "src/tests/suites/network.cpp"
        "src/tests/suites/auth/auth.cpp"
        "src/tests/suites/auth/wy.cpp"
        "src/tests/suites/module.cpp"
        "src/tests/suites/runtime.cpp"
        "src/tests/suites/engine.cpp"
    )
endif()

set(NYX_INTEGRATION_GATE_SRC)
if(NYX_ENABLE_INTEGRATION_GATES)
    list(APPEND NYX_INTEGRATION_GATE_SRC
        "src/tests/integration/auth_gate.cpp"
        "src/tests/integration/release_gate.cpp"
    )
endif()

# 主库源码按领域分组，target 定义处只拼装这些列表
set(NYX_BRIDGE_SRC
    "src/app_bridge/android_context.cpp"
    "src/app_bridge/auth_config.cpp"
    "src/app_bridge/auth.cpp"
    "src/app_bridge/jni.cpp"
    "src/app_bridge/runtime.cpp"
    "src/app_bridge/surface.cpp"
    "src/app_bridge/test.cpp"
)

set(NYX_CORE_SRC
    "src/core/context.cpp"
    "src/core/dispatcher.cpp"
    "src/core/manager/module_controller.cpp"
    "src/core/manager/module_registration.cpp"
)

set(NYX_AUTH_SRC
    "src/core/auth/auth_context.cpp"
    "src/core/auth/auth_client.cpp"
    "src/core/auth/auth_facade.cpp"
    "src/core/auth/auth_session.cpp"
    "src/core/auth/auth_store.cpp"
    "src/core/auth/auth_device.cpp"
    ${NYX_AUTH_MOCK_SRC}
    "src/core/auth/providers/wy/wy_codec.cpp"
    "src/core/auth/providers/wy/wy_profile.cpp"
    "src/core/auth/providers/wy/wy_provider.cpp"
    "src/core/auth/providers/wy/wy_request.cpp"
    "src/core/auth/providers/wy/wy_response.cpp"
    "src/core/auth/providers/wy/wy_verify.cpp"
)

set(NYX_CRYPTO_SRC
    "src/crypto/aes_256_gcm.cpp"
    "src/crypto/blake2b.cpp"
    "src/crypto/xchacha20_poly1305.cpp"
    "src/crypto/md5.cpp"
    "src/crypto/rc4.cpp"
    "src/crypto/sha256.cpp"
    "src/crypto/client/http.cpp"
)

set(NYX_ENGINE_SRC
    "src/engines/engine_sdk.cpp"
    "src/engines/unity/unity_binding.cpp"
    "src/engines/unity/il2cpp_runtime.cpp"
    "src/engines/unity/il2cpp_resolver.cpp"
    "src/engines/unity/unity_signature.cpp"
    "src/engines/unreal/ue_runtime.cpp"
)

set(NYX_OVERLAY_SRC
    "src/overlay/glsurface/glsurface_host.cpp"
    "src/overlay/imgui_bind/imgui_bridge.cpp"
    "src/overlay/input/input_router.cpp"
    "src/overlay/ui/font_registry.cpp"
    "src/overlay/ui/gif_player.cpp"
    "src/overlay/ui/widgets.cpp"
)

set(NYX_RUNTIME_SRC
    "src/runtime/hook/hook_api.cpp"
    "src/runtime/hook/plt_backend.cpp"
    "src/runtime/hook/inline_backend.cpp"
    "src/runtime/hook/hook_registry.cpp"
    "src/runtime/loader/native_library.cpp"
    "src/runtime/memory/memory_linker.cpp"
    "src/runtime/memory/memory_api.cpp"
    "src/runtime/memory/memory_area.cpp"
    "src/runtime/memory/memory_freeze.cpp"
    "src/runtime/memory/memory_improve.cpp"
    "src/runtime/memory/memory_inotify.cpp"
    "src/runtime/memory/memory_normalizer.cpp"
    "src/runtime/memory/memory_map.cpp"
    "src/runtime/memory/memory_procfs.cpp"
    "src/runtime/memory/memory_process.cpp"
    "src/runtime/memory/memory_protect.cpp"
    "src/runtime/memory/memory_reader.cpp"
    "src/runtime/memory/memory_scan.cpp"
    "src/runtime/memory/memory_system.cpp"
    "src/runtime/memory/memory_value.cpp"
    "src/runtime/memory/memory_writer.cpp"
    "src/runtime/memory/memory_attributes.cpp"
    "src/runtime/memory/memory_snapshot.cpp"
    "src/runtime/memory/memory_permission.cpp"
    "src/runtime/memory/memory_transaction.cpp"
    "src/runtime/stack/stack_api.cpp"
    "src/runtime/stack/stack_base.cpp"
    "src/runtime/stack/stack_context.cpp"
    "src/runtime/stack/stack_hooks.cpp"
    "src/runtime/stack/stack_normalizer.cpp"
    "src/runtime/stack/stack_repair.cpp"
    "src/runtime/stack/stack_trace.cpp"
    "src/runtime/vfs/io_redirector.cpp"
    "src/runtime/vfs/module_patch.cpp"
    "src/runtime/vfs/path_mapper.cpp"
    "src/runtime/vfs/path_policy.cpp"
    "src/runtime/vfs/vfs_api.cpp"
)

set(NYX_UTIL_SRC
    "src/utils/json_utils.cpp"
    "src/utils/logger.cpp"
    "src/utils/random.cpp"
    "src/utils/string/format.cpp"
    "src/utils/string/hex.cpp"
    "src/utils/string/utf8.cpp"
)

# 读取 active mod，校验 mod.properties / mod.cmake，并解析 mod 源码列表
nyx_configure_active_mod("${CMAKE_CURRENT_SOURCE_DIR}")

# native 测试探针只在 Debug 测试构建存在，Release 不生成这类额外入口
if(NYX_ENABLE_NATIVE_TESTS AND NOT NYX_RELEASE_BUILD)
    add_library(
        nyx_test_probe
        SHARED
        "src/tests/probes/native_probe.cpp"
        "src/tests/probes/unity_probe.cpp"
        "src/engines/unity/unity_binding.cpp"
        "src/engines/unity/il2cpp_runtime.cpp"
        "src/engines/unity/il2cpp_resolver.cpp"
        "src/engines/unity/unity_signature.cpp"
        "src/runtime/hook/hook_api.cpp"
        "src/runtime/hook/hook_registry.cpp"
        "src/runtime/hook/inline_backend.cpp"
        "src/runtime/hook/plt_backend.cpp"
        "src/runtime/loader/native_library.cpp"
        "src/runtime/memory/memory_linker.cpp"
        "src/runtime/memory/memory_map.cpp"
        "src/runtime/memory/memory_procfs.cpp"
        "src/runtime/memory/memory_process.cpp"
        "src/runtime/stack/stack_base.cpp"
        "src/runtime/stack/stack_hooks.cpp"
        "src/utils/logger.cpp"
        "src/utils/string/format.cpp"
    )
    target_include_directories(
        nyx_test_probe
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    nyx_apply_test_probe_definitions(nyx_test_probe)
    target_link_libraries(
        nyx_test_probe
        PRIVATE
        ${log-lib}
        ${dl-lib}
        vendor_dobby
    )
    nyx_apply_native_defaults(nyx_test_probe)
endif()

# 当前 mod 的最终 so：核心、runtime、vendor 依赖和 mod 私有源码都汇入这里
add_library(
    ${NYX_APP_TARGET}
    SHARED
    ${NYX_BRIDGE_SRC}
    ${NYX_CORE_SRC}
    ${NYX_AUTH_SRC}
    ${NYX_CRYPTO_SRC}
    ${NYX_ENGINE_SRC}
    ${NYX_OVERLAY_SRC}
    ${NYX_RUNTIME_SRC}
    ${NYX_UTIL_SRC}
    ${NYX_BENCHMARK_SRC}
    ${NYX_NATIVE_TEST_SRC}
    ${NYX_INTEGRATION_GATE_SRC}
    ${NYX_MOD_RESOLVED_SOURCES}
)
if(TARGET nyx_test_probe)
    add_dependencies(${NYX_APP_TARGET} nyx_test_probe)
endif()
if(TARGET vendor_shadowhook_nothing)
    add_dependencies(${NYX_APP_TARGET} vendor_shadowhook_nothing)
endif()

# 主 so 也套统一 native 默认值，包括 hidden visibility、section 裁剪和 Release strip
nyx_apply_native_defaults(${NYX_APP_TARGET})
nyx_apply_allvm_release_obfuscation(${NYX_APP_TARGET})
nyx_apply_allvm_full_vmp(${NYX_APP_TARGET})

target_include_directories(
    ${NYX_APP_TARGET}
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(${NYX_APP_TARGET} PROPERTIES OUTPUT_NAME "${NYX_MOD_OUTPUT_NAME}")

# 先注入框架级构建宏，再叠加 active mod 的功能开关、鉴权配置和自定义 hook
nyx_apply_framework_definitions(${NYX_APP_TARGET})
nyx_apply_mod_properties(${NYX_APP_TARGET})

# 主 so 的链接依赖：系统库、加密/网络/UI vendor、inline/PLT hook 后端
target_link_libraries(
    ${NYX_APP_TARGET}
    PRIVATE
    ${log-lib}
    ${dl-lib}
    android
    EGL
    GLESv3
    vendor_json
    vendor_monocypher
    vendor_mbedtls
    vendor_httplib
    vendor_imgui
    vendor_dobby
    vendor_bytehook
)
