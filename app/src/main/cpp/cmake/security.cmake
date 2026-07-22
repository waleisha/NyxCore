include_guard(GLOBAL)

# 默认隐藏 native 符号，只有显式标记的入口才对外可见
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# inline 函数也不要落进动态符号表，减少可枚举的 C++ 细节
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# 所有 native 目标默认生成 PIC，静态库后续合进 so 时不再单独补属性
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 配置构建开关：Debug 保留测试入口，Release 只留下运行必需代码
macro(nyx_configure_build_switches)
    # Android Gradle 传入的是单配置构建类型，这里统一成大写方便判断
    string(TOUPPER "${CMAKE_BUILD_TYPE}" NYX_BUILD_TYPE)

    # Debug 宏给 C++ 里做轻量分支，不依赖编译器自己的 NDEBUG
    set(NYX_DEBUG_BUILD OFF)
    if(NYX_BUILD_TYPE STREQUAL "DEBUG")
        set(NYX_DEBUG_BUILD ON)
    endif()

    # Release / RelWithDebInfo / MinSizeRel 都按发布构建处理
    set(NYX_RELEASE_BUILD OFF)
    if(NYX_BUILD_TYPE MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
        set(NYX_RELEASE_BUILD ON)
    endif()

    # 默认值先按调试体验打开，下面遇到发布构建再统一收紧
    set(NYX_ENABLE_NATIVE_TESTS_DEFAULT ON)
    set(NYX_ENABLE_INTEGRATION_GATES_DEFAULT ON)
    set(NYX_ENABLE_BENCHMARKS_DEFAULT ON)
    set(NYX_AUTH_ENABLE_MOCK_DEFAULT ON)
    set(NYX_ENABLE_NATIVE_LOGS_DEFAULT ON)

    # 发布包不能带测试入口、集成闸门、性能计时和 mock 鉴权
    if(NYX_RELEASE_BUILD)
        set(NYX_ENABLE_NATIVE_TESTS_DEFAULT OFF)
        set(NYX_ENABLE_INTEGRATION_GATES_DEFAULT OFF)
        set(NYX_ENABLE_BENCHMARKS_DEFAULT OFF)
        set(NYX_AUTH_ENABLE_MOCK_DEFAULT OFF)
        set(NYX_ENABLE_NATIVE_LOGS_DEFAULT OFF)
    endif()

    # 这些 CACHE 开关可被 Gradle 或命令行覆盖，Release 下会再做强校验
    set(NYX_ENABLE_NATIVE_TESTS ${NYX_ENABLE_NATIVE_TESTS_DEFAULT} CACHE BOOL "编译 native 自检和测试支持")
    set(NYX_ENABLE_INTEGRATION_GATES ${NYX_ENABLE_INTEGRATION_GATES_DEFAULT} CACHE BOOL "暴露 instrumentation tests 使用的集成/发布闸门")
    set(NYX_ENABLE_BENCHMARKS ${NYX_ENABLE_BENCHMARKS_DEFAULT} CACHE BOOL "启用 native 性能计时")
    set(NYX_AUTH_ENABLE_MOCK ${NYX_AUTH_ENABLE_MOCK_DEFAULT} CACHE BOOL "启用测试用鉴权 Mock provider")
    set(NYX_ENABLE_NATIVE_LOGS ${NYX_ENABLE_NATIVE_LOGS_DEFAULT} CACHE BOOL "启用 native 日志输出")

    # 发布构建发现测试开关或日志开关被强行打开时直接中断，避免生成可误发的 so。
    foreach(option_name IN ITEMS
        NYX_ENABLE_NATIVE_TESTS
        NYX_ENABLE_INTEGRATION_GATES
        NYX_ENABLE_BENCHMARKS
        NYX_AUTH_ENABLE_MOCK
        NYX_ENABLE_NATIVE_LOGS
    )
        if(NYX_RELEASE_BUILD AND ${option_name})
            message(FATAL_ERROR "${option_name} must stay OFF for release builds")
        endif()
    endforeach()

    set(NYX_NATIVE_LOGS_MODE 0)
    if(NYX_ENABLE_NATIVE_LOGS)
        set(NYX_NATIVE_LOGS_MODE 1)
    endif()
    add_compile_definitions(NYX_ENABLE_NATIVE_LOGS=${NYX_NATIVE_LOGS_MODE})

    # 集成闸门调用的是 native test 支撑代码，不能单独打开
    if(NYX_ENABLE_INTEGRATION_GATES AND NOT NYX_ENABLE_NATIVE_TESTS)
        message(FATAL_ERROR "NYX_ENABLE_INTEGRATION_GATES requires NYX_ENABLE_NATIVE_TESTS")
    endif()

    # 链接优化：和 -ffunction-sections/-fdata-sections 配合，丢掉没有被引用的函数和数据
    set(NYX_NATIVE_GC_SECTIONS ON CACHE BOOL "链接时丢弃未使用的 native section")

    # 静态库符号隐藏：把 .a 里的第三方符号收进内部，减少动态符号表暴露面
    set(NYX_NATIVE_EXCLUDE_STATIC_LIBS ON CACHE BOOL "隐藏链接进来的静态库符号")

    # 依赖裁剪：没有实际用到的 shared library 不写入 DT_NEEDED
    set(NYX_NATIVE_AS_NEEDED ON CACHE BOOL "删除未使用的 shared library 依赖")

    # 发布包 strip：剥离普通符号表，体积更小，也不方便直接枚举内部函数名
    set(NYX_NATIVE_RELEASE_STRIP ON CACHE BOOL "Release 构建剥离 native 符号")

    # 全局 strip：调试会变困难，通常只在临时验证最终体积时打开
    set(NYX_NATIVE_STRIP_SYMBOLS OFF CACHE BOOL "所有构建类型都剥离 native 符号")

    # LLD 链接优化：只在发布构建追加 -Wl,-O2，避免拖慢日常 Debug 链接
    set(NYX_NATIVE_LINK_OPTIMIZE ON CACHE BOOL "Release 构建启用链接器优化")

    # 安全 ICF：只折叠链接器能证明等价的代码段，不使用 aggressive/all 模式
    set(NYX_NATIVE_SAFE_ICF ON CACHE BOOL "安全折叠相同 native 代码段")

    # 栈保护：给高风险栈帧加 canary，越界覆盖返回前会先崩溃
    set(NYX_NATIVE_STACK_PROTECTOR ON CACHE BOOL "启用 native 栈保护")

    # Fortify：优化构建下替换部分 libc 调用，能在运行时拦住明显越界
    set(NYX_NATIVE_FORTIFY ON CACHE BOOL "优化构建启用 _FORTIFY_SOURCE")

    # ALLVM：常规混淆和 VMP 分开控制；VMP 可自动收集函数，也可用显式名单覆盖。
    # 常规 ALLVM 会影响当前 ImGui 渲染链路，Release 默认关闭，需要保护时再显式打开。
    set(NYX_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV 2)
    if(DEFINED CACHE{NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV})
        get_property(
            NYX_CACHED_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV
            CACHE NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV
            PROPERTY VALUE
        )
    else()
        set(NYX_CACHED_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV "")
    endif()
    if(NOT NYX_CACHED_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV STREQUAL "${NYX_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV}")
        set(NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION OFF CACHE BOOL "Release 构建启用 ALLVM 常规混淆组合" FORCE)
        set(
            NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV
            "${NYX_ALLVM_RELEASE_OBFUSCATION_DEFAULT_REV}"
            CACHE INTERNAL "NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION default revision"
        )
    else()
        set(NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION OFF CACHE BOOL "Release 构建启用 ALLVM 常规混淆组合")
    endif()
    set(NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES)
    set(NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES)
    set(NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES)
    set(NYX_NATIVE_ALLVM_OBFUSCATION_SKIP_SOURCES "" CACHE STRING "不套 ALLVM 常规混淆的源码列表，仍可参与 VMP")
    set(NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_SOURCES "" CACHE STRING "只套轻量 ALLVM 常规混淆的源码列表")
    set(NYX_NATIVE_ALLVM_VMP_SKIP_SOURCES "" CACHE STRING "不套 ALLVM VMP 的源码列表")
    set(NYX_NATIVE_ALLVM_ENABLE_FLA OFF CACHE BOOL "Release 构建启用 ALLVM 控制流平坦化")
    set(NYX_NATIVE_ALLVM_ENABLE_INDBR ON CACHE BOOL "Release 构建启用 ALLVM 间接跳转")
    set(NYX_NATIVE_ALLVM_ENABLE_ICALL OFF CACHE BOOL "Release 构建启用 ALLVM 间接调用")
    set(NYX_NATIVE_ALLVM_ENABLE_INDGV ON CACHE BOOL "Release 构建启用 ALLVM 间接全局变量")
    set(NYX_NATIVE_ALLVM_ENABLE_CSE ON CACHE BOOL "Release 构建启用 ALLVM 字符串加密")
    set(NYX_NATIVE_ALLVM_ENABLE_CIE ON CACHE BOOL "Release 构建启用 ALLVM 整数常量加密")
    set(NYX_NATIVE_ALLVM_ENABLE_CFE ON CACHE BOOL "Release 构建启用 ALLVM 浮点常量加密")
    set(NYX_NATIVE_ALLVM_ENABLE_RTTI_ERASE OFF CACHE BOOL "Release 构建启用 ALLVM RTTI 擦除")
    set(NYX_NATIVE_ALLVM_ENABLE_SYSCALL_PROTECT OFF CACHE BOOL "Release 构建启用 ALLVM syscall 保护")
    set(NYX_NATIVE_ALLVM_ENABLE_IDA ON CACHE BOOL "Release 构建启用 ALLVM IDA/TracerPid 检测")
    set(NYX_NATIVE_ALLVM_ENABLE_TIME ON CACHE BOOL "Release 构建启用 ALLVM 时间差检测")
    set(NYX_NATIVE_ALLVM_ENABLE_LDPRELOAD ON CACHE BOOL "Release 构建启用 ALLVM LD_PRELOAD 检测")
    set(NYX_NATIVE_ALLVM_ENABLE_VMDETECT ON CACHE BOOL "Release 构建启用 ALLVM 虚拟机/模拟器检测")
    set(NYX_NATIVE_ALLVM_ENABLE_HOSTS ON CACHE BOOL "Release 构建启用 ALLVM hosts 篡改检测")
    set(NYX_NATIVE_ALLVM_ENABLE_BANDUMP ON CACHE BOOL "Release 构建启用 ALLVM 内存 dump 保护")
    set(NYX_NATIVE_ALLVM_ENABLE_HIDEMAPS OFF CACHE BOOL "Release 构建启用 ALLVM 隐藏 maps")
    set(NYX_NATIVE_ALLVM_ENABLE_FAKEMAPS ON CACHE BOOL "Release 构建启用 ALLVM 伪造 maps")
    set(NYX_NATIVE_ALLVM_LEVEL_FLA 1 CACHE STRING "ALLVM 控制流平坦化强度")
    set(NYX_NATIVE_ALLVM_LEVEL_INDBR 1 CACHE STRING "ALLVM 间接跳转强度")
    set(NYX_NATIVE_ALLVM_LEVEL_ICALL 1 CACHE STRING "ALLVM 间接调用强度")
    set(NYX_NATIVE_ALLVM_LEVEL_INDGV 1 CACHE STRING "ALLVM 间接全局变量强度")
    set(NYX_NATIVE_ALLVM_LEVEL_CIE 3 CACHE STRING "ALLVM 整数常量加密强度")
    set(NYX_NATIVE_ALLVM_LEVEL_CFE 3 CACHE STRING "ALLVM 浮点常量加密强度")
    set(NYX_NATIVE_ALLVM_FULL_VMP OFF CACHE BOOL "Release 构建启用 ALLVM 全量 VMP")
    set(NYX_NATIVE_ALLVM_VMP_NOINLINE ON CACHE BOOL "ALLVM VMP 下禁止被保护函数内联")
    set(NYX_NATIVE_ALLVM_VMP_FUNCTIONS "" CACHE STRING "显式指定 ALLVM VMP 函数名列表，空值时从目标源码粗略收集")
endmacro()

# 配置测试探针宏：C++ 和 instrumentation test 用同一组库名/符号名
macro(nyx_configure_test_probe_macros)
    set(NYX_TEST_PROBE_LIBRARY "libnyx_test_probe.so")
    set(NYX_RUNTIME_PROBE_LIBRARY "${NYX_TEST_PROBE_LIBRARY}")
    set(NYX_RUNTIME_PROBE_SYMBOL "nyx_runtime_probe_value")
    set(NYX_RUNTIME_PROBE_EXPECTED 20260712)
    set(NYX_PLT_CALLEE_LIBRARY "")
    set(NYX_PLT_CALLER_LIBRARY "${NYX_TEST_PROBE_LIBRARY}")
    set(NYX_PLT_CALL_SYMBOL "nyx_plt_call_probe_value")
    set(NYX_PLT_TARGET_SYMBOL "getpid")
    set(NYX_PLT_PROBE_INPUT 7)
    set(NYX_PLT_PROBE_EXPECTED 41)
    set(NYX_PLT_REPLACEMENT_EXPECTED 82)
    set(NYX_VFS_CALLER_LIBRARY "${NYX_TEST_PROBE_LIBRARY}")
    set(NYX_VFS_OPEN_WRITE_SYMBOL "nyx_vfs_open_write_probe")
    set(NYX_VFS_OPENAT_WRITE_SYMBOL "nyx_vfs_openat_write_probe")
    set(NYX_VFS_STAT_SYMBOL "nyx_vfs_stat_probe")
    set(NYX_VFS_LSTAT_SYMBOL "nyx_vfs_lstat_probe")
    set(NYX_VFS_FSTATAT_SYMBOL "nyx_vfs_fstatat_probe")
endmacro()

# 所有公开封装先校验 target，CMake 报错会更靠近真正问题
function(nyx_require_target target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "NyxCore target does not exist: ${target_name}")
    endif()
endfunction()

# 给普通 native target 套一层默认属性、宏和链接优化
function(nyx_apply_native_defaults target_name)
    nyx_require_target("${target_name}")

    get_target_property(target_type "${target_name}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        # INTERFACE target 没有自己的产物，只把编译参数传给链接它的目标
        set(compile_scope INTERFACE)
    else()
        # 非 INTERFACE target 统一语言标准、隐藏符号和 PIC 属性
        set(compile_scope PRIVATE)
        set_target_properties(
            "${target_name}"
            PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            POSITION_INDEPENDENT_CODE ON
        )
    endif()

    # 传给 C++ 的构建模式宏保持 0/1，源码里可以直接 #if 判断
    set(debug_mode 0)
    if(NYX_DEBUG_BUILD)
        set(debug_mode 1)
    endif()

    set(release_mode 0)
    if(NYX_RELEASE_BUILD)
        set(release_mode 1)
    endif()

    target_compile_definitions(
        "${target_name}"
        ${compile_scope}
        NYX_DEBUG_MODE=${debug_mode}
        NYX_RELEASE_MODE=${release_mode}
    )

    # _FORTIFY_SOURCE 依赖优化级别；Debug 下开了也基本没有收益，还可能制造噪声
    if(NYX_NATIVE_FORTIFY AND NYX_RELEASE_BUILD)
        target_compile_definitions("${target_name}" ${compile_scope} _FORTIFY_SOURCE=2)
    endif()

    # 每个函数/数据单独放 section，后面的 --gc-sections 才能按实际引用裁剪
    set(native_compile_options
        "$<$<COMPILE_LANGUAGE:C>:-fvisibility=hidden>"
        "$<$<COMPILE_LANGUAGE:C>:-ffunction-sections>"
        "$<$<COMPILE_LANGUAGE:C>:-fdata-sections>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fvisibility=hidden>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>"
        "$<$<COMPILE_LANGUAGE:CXX>:-ffunction-sections>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fdata-sections>"
    )

    # 栈保护对体积影响很小，默认给 C/C++ 都加上
    if(NYX_NATIVE_STACK_PROTECTOR)
        list(APPEND native_compile_options
            "$<$<COMPILE_LANGUAGE:C>:-fstack-protector-strong>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fstack-protector-strong>"
        )
    endif()

    # 发布包不写 compiler ident，少留一点编译器版本和构建痕迹
    if(NYX_RELEASE_BUILD)
        list(APPEND native_compile_options
            "$<$<COMPILE_LANGUAGE:C>:-fno-ident>"
            "$<$<COMPILE_LANGUAGE:CXX>:-fno-ident>"
        )
    endif()

    target_compile_options("${target_name}" ${compile_scope} ${native_compile_options})

    # 静态库本身不执行链接，只有最终 ELF 产物才追加 linker options
    if(
        target_type STREQUAL "SHARED_LIBRARY"
        OR target_type STREQUAL "MODULE_LIBRARY"
        OR target_type STREQUAL "EXECUTABLE"
    )
        set(native_link_options)

        # Android 新设备可能使用 16KB page size，arm64 so 需要显式兼容
        if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
            list(APPEND native_link_options "-Wl,-z,max-page-size=16384")
        endif()

        # LLD 自身做发布级链接优化，主要减少重定位和无用布局
        if(NYX_RELEASE_BUILD AND NYX_NATIVE_LINK_OPTIMIZE)
            list(APPEND native_link_options "-Wl,-O2")
        endif()

        # 未实际引用的 so 不写进依赖表，加载时也少一次查找
        if(NYX_NATIVE_AS_NEEDED)
            list(APPEND native_link_options "-Wl,--as-needed")
        endif()

        # 第三方静态库符号不导出到最终 so，避免内部实现被外部 dlsym 到
        if(NYX_NATIVE_EXCLUDE_STATIC_LIBS)
            list(APPEND native_link_options "-Wl,--exclude-libs,ALL")
        endif()

        # 丢弃没有被引用的 section，配合上面的 -ffunction-sections/-fdata-sections
        if(NYX_NATIVE_GC_SECTIONS)
            list(APPEND native_link_options "-Wl,--gc-sections")
        endif()

        # 安全折叠相同实现的函数，减少体积；只在 Release 开，方便 Debug 定位
        if(NYX_RELEASE_BUILD AND NYX_NATIVE_SAFE_ICF)
            list(APPEND native_link_options "-Wl,--icf=safe")
        endif()

        # strip 会影响符号排查，所以默认只在 Release 或显式要求时执行
        if(NYX_NATIVE_STRIP_SYMBOLS OR (NYX_RELEASE_BUILD AND NYX_NATIVE_RELEASE_STRIP))
            list(APPEND native_link_options "-Wl,--strip-all")
        endif()

        if(native_link_options)
            target_link_options("${target_name}" PRIVATE ${native_link_options})
        endif()
    endif()
endfunction()

function(nyx_append_allvm_mllvm_option option_list_name option)
    set(options ${${option_list_name}})
    list(APPEND options "-mllvm" "${option}")
    set(${option_list_name} "${options}" PARENT_SCOPE)
endfunction()

function(nyx_write_allvm_response_file response_file)
    file(WRITE "${response_file}" "")
    foreach(option IN LISTS ARGN)
        file(APPEND "${response_file}" "${option}\n")
    endforeach()
endfunction()

function(nyx_normalize_source_key source_path out_name)
    if(IS_ABSOLUTE "${source_path}")
        file(RELATIVE_PATH source_key "${CMAKE_CURRENT_SOURCE_DIR}" "${source_path}")
    else()
        set(source_key "${source_path}")
    endif()

    file(TO_CMAKE_PATH "${source_key}" source_key)
    string(REGEX REPLACE "^\\./" "" source_key "${source_key}")
    set(${out_name} "${source_key}" PARENT_SCOPE)
endfunction()

function(nyx_add_allvm_obfuscation_skip_sources)
    set(skip_sources ${NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES})
    list(APPEND skip_sources ${ARGN})
    if(skip_sources)
        list(REMOVE_DUPLICATES skip_sources)
    endif()
    set(NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES "${skip_sources}" PARENT_SCOPE)
endfunction()

function(nyx_add_allvm_light_obfuscation_sources)
    set(light_sources ${NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES})
    list(APPEND light_sources ${ARGN})
    if(light_sources)
        list(REMOVE_DUPLICATES light_sources)
    endif()
    set(NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES "${light_sources}" PARENT_SCOPE)
endfunction()

function(nyx_add_allvm_vmp_skip_sources)
    set(skip_sources ${NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES})
    list(APPEND skip_sources ${ARGN})
    if(skip_sources)
        list(REMOVE_DUPLICATES skip_sources)
    endif()
    set(NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES "${skip_sources}" PARENT_SCOPE)
endfunction()

# 同时跳过常规 ALLVM 和 VMP；只想跳过常规 ALLVM 时用
# nyx_add_allvm_obfuscation_skip_sources。
function(nyx_add_allvm_skip_sources)
    set(obfuscation_sources ${NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES})
    list(APPEND obfuscation_sources ${ARGN})
    if(obfuscation_sources)
        list(REMOVE_DUPLICATES obfuscation_sources)
    endif()
    set(NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES "${obfuscation_sources}" PARENT_SCOPE)

    set(light_sources ${NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES})
    list(REMOVE_ITEM light_sources ${ARGN})
    set(NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES "${light_sources}" PARENT_SCOPE)

    set(vmp_sources ${NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES})
    list(APPEND vmp_sources ${ARGN})
    if(vmp_sources)
        list(REMOVE_DUPLICATES vmp_sources)
    endif()
    set(NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES "${vmp_sources}" PARENT_SCOPE)
endfunction()

function(nyx_collect_allvm_obfuscation_skip_sources out_name)
    set(skip_sources)
    foreach(skip_source IN LISTS
        NYX_NATIVE_ALLVM_OBFUSCATION_REQUIRED_SKIP_SOURCES
        NYX_NATIVE_ALLVM_OBFUSCATION_SKIP_SOURCES
    )
        nyx_normalize_source_key("${skip_source}" skip_source_key)
        list(APPEND skip_sources "${skip_source_key}")
    endforeach()

    if(skip_sources)
        list(REMOVE_DUPLICATES skip_sources)
    endif()

    set(${out_name} "${skip_sources}" PARENT_SCOPE)
endfunction()

function(nyx_collect_allvm_light_obfuscation_sources out_name)
    set(light_sources)
    foreach(light_source IN LISTS
        NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_REQUIRED_SOURCES
        NYX_NATIVE_ALLVM_LIGHT_OBFUSCATION_SOURCES
    )
        nyx_normalize_source_key("${light_source}" light_source_key)
        list(APPEND light_sources "${light_source_key}")
    endforeach()

    if(light_sources)
        list(REMOVE_DUPLICATES light_sources)
    endif()

    set(${out_name} "${light_sources}" PARENT_SCOPE)
endfunction()

function(nyx_collect_allvm_vmp_skip_sources out_name)
    set(skip_sources)
    foreach(skip_source IN LISTS
        NYX_NATIVE_ALLVM_VMP_REQUIRED_SKIP_SOURCES
        NYX_NATIVE_ALLVM_VMP_SKIP_SOURCES
    )
        nyx_normalize_source_key("${skip_source}" skip_source_key)
        list(APPEND skip_sources "${skip_source_key}")
    endforeach()

    if(skip_sources)
        list(REMOVE_DUPLICATES skip_sources)
    endif()

    set(${out_name} "${skip_sources}" PARENT_SCOPE)
endfunction()

function(nyx_collect_source_function_names source_path out_name)
    set(function_names)
    if(IS_ABSOLUTE "${source_path}")
        set(resolved_source "${source_path}")
    else()
        set(resolved_source "${CMAKE_CURRENT_SOURCE_DIR}/${source_path}")
    endif()

    if(NOT EXISTS "${resolved_source}")
        set(${out_name} "" PARENT_SCOPE)
        return()
    endif()

    set(skip_names
        if
        for
        while
        switch
        catch
        return
        sizeof
        static_cast
        reinterpret_cast
        const_cast
        dynamic_cast
    )

    file(READ "${resolved_source}" source_text)
    string(REPLACE "\r\n" "\n" source_text "${source_text}")
    string(REPLACE "\r" "\n" source_text "${source_text}")
    string(REPLACE "\n" ";" source_lines "${source_text}")

    set(signature "")
    foreach(source_line IN LISTS source_lines)
        string(REGEX REPLACE "//.*$" "" source_line "${source_line}")
        string(STRIP "${source_line}" source_line)
        if(source_line STREQUAL "" OR source_line MATCHES "^#")
            continue()
        endif()

        if(signature STREQUAL "")
            set(signature "${source_line}")
        else()
            string(APPEND signature " ${source_line}")
        endif()

        if(signature MATCHES ";")
            set(signature "")
            continue()
        endif()

        if(NOT signature MATCHES "\\{")
            continue()
        endif()

        if(signature MATCHES "=")
            set(signature "")
            continue()
        endif()

        if(signature MATCHES "^[A-Za-z_~][A-Za-z0-9_:~]*[ \t]*\\([^()]*\\)[^{};]*\\{")
            string(REGEX REPLACE "^([A-Za-z_~][A-Za-z0-9_:~]*)[ \t]*\\([^()]*\\).*$" "\\1" function_name "${signature}")
        elseif(signature MATCHES "[^A-Za-z0-9_:~]([A-Za-z_~][A-Za-z0-9_:~]*)[ \t]*\\([^()]*\\)[^{};]*\\{")
            string(REGEX REPLACE "^.*[^A-Za-z0-9_:~]([A-Za-z_~][A-Za-z0-9_:~]*)[ \t]*\\([^()]*\\).*$" "\\1" function_name "${signature}")
        else()
            set(signature "")
            continue()
        endif()

        string(REGEX REPLACE "^.*::" "" function_name "${function_name}")
        if(function_name IN_LIST skip_names OR function_name STREQUAL "")
            set(signature "")
            continue()
        endif()

        list(APPEND function_names "${function_name}")
        set(signature "")
    endforeach()

    if(function_names)
        list(REMOVE_DUPLICATES function_names)
        list(SORT function_names)
    endif()

    set(${out_name} "${function_names}" PARENT_SCOPE)
endfunction()

function(nyx_collect_allvm_light_options out_name)
    set(light_options)
    nyx_append_allvm_mllvm_option(light_options "-irobf")
    nyx_append_allvm_mllvm_option(light_options "-irobf-cse")
    nyx_append_allvm_mllvm_option(light_options "-irobf-cie")
    nyx_append_allvm_mllvm_option(light_options "-level-cie=${NYX_NATIVE_ALLVM_LEVEL_CIE}")
    nyx_append_allvm_mllvm_option(light_options "-irobf-cfe")
    nyx_append_allvm_mllvm_option(light_options "-level-cfe=${NYX_NATIVE_ALLVM_LEVEL_CFE}")
    nyx_append_allvm_mllvm_option(light_options "-irobf-ida")
    set(${out_name} "${light_options}" PARENT_SCOPE)
endfunction()

function(nyx_apply_allvm_release_obfuscation target_name)
    nyx_require_target("${target_name}")

    if(NOT NYX_RELEASE_BUILD OR NOT NYX_NATIVE_ALLVM_RELEASE_OBFUSCATION)
        return()
    endif()

    get_target_property(target_type "${target_name}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    set(allvm_options)
    nyx_append_allvm_mllvm_option(allvm_options "-irobf")

    if(NYX_NATIVE_ALLVM_ENABLE_FLA)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-fla")
        nyx_append_allvm_mllvm_option(allvm_options "-level-fla=${NYX_NATIVE_ALLVM_LEVEL_FLA}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_INDBR)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-indbr")
        nyx_append_allvm_mllvm_option(allvm_options "-level-indbr=${NYX_NATIVE_ALLVM_LEVEL_INDBR}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_ICALL)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-icall")
        nyx_append_allvm_mllvm_option(allvm_options "-level-icall=${NYX_NATIVE_ALLVM_LEVEL_ICALL}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_INDGV)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-indgv")
        nyx_append_allvm_mllvm_option(allvm_options "-level-indgv=${NYX_NATIVE_ALLVM_LEVEL_INDGV}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_CSE)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-cse")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_CIE)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-cie")
        nyx_append_allvm_mllvm_option(allvm_options "-level-cie=${NYX_NATIVE_ALLVM_LEVEL_CIE}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_CFE)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-cfe")
        nyx_append_allvm_mllvm_option(allvm_options "-level-cfe=${NYX_NATIVE_ALLVM_LEVEL_CFE}")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_RTTI_ERASE)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-rtti")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_SYSCALL_PROTECT)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-syscall")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_IDA)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-ida")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_TIME)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-time")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_LDPRELOAD)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-ldpreload")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_VMDETECT)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-vmdetect")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_HOSTS)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-hosts")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_BANDUMP)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-bandump")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_HIDEMAPS)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-hidemaps")
    endif()

    if(NYX_NATIVE_ALLVM_ENABLE_FAKEMAPS)
        nyx_append_allvm_mllvm_option(allvm_options "-irobf-fakemaps")
    endif()

    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" allvm_response_name "${target_name}")
    set(allvm_response_file "${CMAKE_CURRENT_BINARY_DIR}/${allvm_response_name}_allvm_obfuscation.rsp")
    nyx_write_allvm_response_file("${allvm_response_file}" ${allvm_options})

    nyx_collect_allvm_light_options(light_options)
    set(light_response_file "${CMAKE_CURRENT_BINARY_DIR}/${allvm_response_name}_allvm_light_obfuscation.rsp")
    nyx_write_allvm_response_file("${light_response_file}" ${light_options})

    list(LENGTH allvm_options allvm_option_count)
    math(EXPR allvm_pass_count "${allvm_option_count} / 2")

    list(LENGTH light_options light_option_count)
    math(EXPR light_pass_count "${light_option_count} / 2")

    nyx_collect_allvm_obfuscation_skip_sources(skip_sources)
    nyx_collect_allvm_light_obfuscation_sources(light_sources)

    get_target_property(target_sources "${target_name}" SOURCES)
    set(applied_count 0)
    set(light_count 0)
    set(skipped_count 0)
    foreach(source_path IN LISTS target_sources)
        if(NOT source_path MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
            continue()
        endif()

        nyx_normalize_source_key("${source_path}" source_key)
        if(source_key IN_LIST skip_sources)
            math(EXPR skipped_count "${skipped_count} + 1")
            continue()
        endif()

        if(source_key IN_LIST light_sources)
            set_property(SOURCE "${source_path}" APPEND PROPERTY COMPILE_OPTIONS "@${light_response_file}")
            math(EXPR light_count "${light_count} + 1")
        else()
            set_property(SOURCE "${source_path}" APPEND PROPERTY COMPILE_OPTIONS "@${allvm_response_file}")
            math(EXPR applied_count "${applied_count} + 1")
        endif()
    endforeach()

    message(STATUS "ALLVM release obfuscation enabled for ${target_name}: ${allvm_pass_count} full mllvm option(s), ${applied_count} full source file(s), ${light_pass_count} light mllvm option(s), ${light_count} light source file(s), ${skipped_count} skipped")
endfunction()

# 收集 VMP 函数名：默认从目标源码提取；NYX_NATIVE_ALLVM_VMP_FUNCTIONS 非空时跳过这里。
function(nyx_collect_allvm_vmp_functions target_name out_name)
    nyx_require_target("${target_name}")

    get_target_property(target_sources "${target_name}" SOURCES)
    if(NOT target_sources)
        set(${out_name} "" PARENT_SCOPE)
        return()
    endif()

    set(vmp_functions)
    nyx_collect_allvm_vmp_skip_sources(skip_sources)

    foreach(source_path IN LISTS target_sources)
        if(NOT source_path MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
            continue()
        endif()

        nyx_normalize_source_key("${source_path}" source_key)
        if(source_key IN_LIST skip_sources)
            continue()
        endif()

        nyx_collect_source_function_names("${source_path}" source_functions)
        list(APPEND vmp_functions ${source_functions})
    endforeach()

    if(vmp_functions)
        list(REMOVE_DUPLICATES vmp_functions)
        list(SORT vmp_functions)
    endif()

    set(${out_name} "${vmp_functions}" PARENT_SCOPE)
endfunction()

function(nyx_apply_allvm_full_vmp target_name)
    nyx_require_target("${target_name}")

    if(NOT NYX_RELEASE_BUILD OR NOT NYX_NATIVE_ALLVM_FULL_VMP)
        return()
    endif()

    get_target_property(target_type "${target_name}" TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        return()
    endif()

    set(vmp_functions ${NYX_NATIVE_ALLVM_VMP_FUNCTIONS})
    if(NOT vmp_functions)
        nyx_collect_allvm_vmp_functions("${target_name}" vmp_functions)
    endif()

    if(NOT vmp_functions)
        message(WARNING "ALLVM full VMP enabled, but no functions were collected for ${target_name}")
        return()
    endif()

    list(LENGTH vmp_functions vmp_function_count)
    string(JOIN ";" vmp_function_arg ${vmp_functions})

    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" vmp_response_name "${target_name}")
    set(vmp_response_file "${CMAKE_CURRENT_BINARY_DIR}/${vmp_response_name}_allvm_vmp.rsp")
    file(WRITE "${vmp_response_file}"
        "-mllvm\n"
        "-irobf\n"
        "-mllvm\n"
        "-irobf-vmp\n"
        "-mllvm\n"
        "-irobf-vm_functions=${vmp_function_arg}\n"
    )

    if(NYX_NATIVE_ALLVM_VMP_NOINLINE)
        file(APPEND "${vmp_response_file}"
            "-mllvm\n"
            "-irobf-vmp-noinline\n"
        )
    endif()

    nyx_collect_allvm_vmp_skip_sources(skip_sources)

    get_target_property(target_sources "${target_name}" SOURCES)
    set(applied_count 0)
    set(skipped_count 0)
    foreach(source_path IN LISTS target_sources)
        if(NOT source_path MATCHES "\\.(c|cc|cpp|cxx|m|mm)$")
            continue()
        endif()

        nyx_normalize_source_key("${source_path}" source_key)
        if(source_key IN_LIST skip_sources)
            math(EXPR skipped_count "${skipped_count} + 1")
            continue()
        endif()

        set_property(SOURCE "${source_path}" APPEND PROPERTY COMPILE_OPTIONS "@${vmp_response_file}")
        math(EXPR applied_count "${applied_count} + 1")
    endforeach()

    message(STATUS "ALLVM full VMP enabled for ${target_name}: ${vmp_function_count} function name(s), ${applied_count} source file(s), ${skipped_count} skipped")
endfunction()

# 极小 so 默认值：给 stub/占位 so 用，目标是能加载、体积小、不依赖运行库
function(nyx_apply_tiny_shared_defaults target_name)
    nyx_require_target("${target_name}")

    # tiny so 仍然是动态库，必须保持 PIC
    set_target_properties(
        "${target_name}"
        PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # 这类目标不需要异常展开和调试身份信息，优先压体积
    target_compile_options(
        "${target_name}"
        PRIVATE
        -Oz
        -fno-ident
        -fno-unwind-tables
        -fno-asynchronous-unwind-tables
    )

    # 不链接默认启动文件和标准库，只保留最小 ELF 外壳
    set(tiny_link_options
        "-Wl,--strip-all"
        "-Wl,--as-needed"
        "-Wl,--hash-style=sysv"
        "-Wl,--build-id=none"
        "-Wl,-nostdlib"
        "-nostartfiles"
        "-nodefaultlibs"
    )

    # tiny so 也要跟主 so 一样兼容 16KB page size
    if(ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
        list(APPEND tiny_link_options "-Wl,-z,max-page-size=16384")
    endif()

    target_link_options("${target_name}" PRIVATE ${tiny_link_options})
endfunction()

# 将 CMake bool 转成 C++ 里稳定可用的 NAME=0/1 宏
function(nyx_bool_definition target_name name value)
    nyx_require_target("${target_name}")

    if(${value})
        target_compile_definitions("${target_name}" PRIVATE ${name}=1)
    else()
        target_compile_definitions("${target_name}" PRIVATE ${name}=0)
    endif()
endfunction()

# 批量注入同名 bool 宏，调用处只列变量名即可
function(nyx_bool_definitions target_name)
    nyx_require_target("${target_name}")

    foreach(name IN LISTS ARGN)
        nyx_bool_definition("${target_name}" "${name}" "${name}")
    endforeach()
endfunction()

# 注入普通 PRIVATE 宏，集中走 target 校验
function(nyx_private_definitions target_name)
    nyx_require_target("${target_name}")

    target_compile_definitions("${target_name}" PRIVATE ${ARGN})
endfunction()

# 测试探针自身只需要期望值和 hook 后端开关
function(nyx_apply_test_probe_definitions target_name)
    nyx_private_definitions(
        "${target_name}"
        NYX_RUNTIME_PROBE_EXPECTED=${NYX_RUNTIME_PROBE_EXPECTED}
        NYX_PLT_PROBE_INPUT=${NYX_PLT_PROBE_INPUT}
        NYX_PLT_PROBE_EXPECTED=${NYX_PLT_PROBE_EXPECTED}
        NYX_PLT_HOOK_HAS_BYTEHOOK=0
        NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER=0
    )
endfunction()

# 主 so 的框架级构建宏：调试入口、闸门、benchmark、mock provider 和测试探针
function(nyx_apply_framework_definitions target_name)
    nyx_bool_definitions(
        "${target_name}"
        NYX_ENABLE_NATIVE_TESTS
        NYX_ENABLE_INTEGRATION_GATES
        NYX_ENABLE_BENCHMARKS
        NYX_AUTH_ENABLE_MOCK
    )
    nyx_apply_runtime_probe_definitions("${target_name}")
endfunction()

# 主 so 需要完整探针配置，运行时测试会按这些名字查库和符号
function(nyx_apply_runtime_probe_definitions target_name)
    nyx_private_definitions(
        "${target_name}"
        NYX_TEST_PROBE_LIBRARY="${NYX_TEST_PROBE_LIBRARY}"
        NYX_RUNTIME_PROBE_LIBRARY="${NYX_RUNTIME_PROBE_LIBRARY}"
        NYX_RUNTIME_PROBE_SYMBOL="${NYX_RUNTIME_PROBE_SYMBOL}"
        NYX_RUNTIME_PROBE_EXPECTED=${NYX_RUNTIME_PROBE_EXPECTED}
        NYX_PLT_CALLEE_LIBRARY="${NYX_PLT_CALLEE_LIBRARY}"
        NYX_PLT_CALLER_LIBRARY="${NYX_PLT_CALLER_LIBRARY}"
        NYX_PLT_CALL_SYMBOL="${NYX_PLT_CALL_SYMBOL}"
        NYX_PLT_TARGET_SYMBOL="${NYX_PLT_TARGET_SYMBOL}"
        NYX_PLT_PROBE_INPUT=${NYX_PLT_PROBE_INPUT}
        NYX_PLT_PROBE_EXPECTED=${NYX_PLT_PROBE_EXPECTED}
        NYX_PLT_REPLACEMENT_EXPECTED=${NYX_PLT_REPLACEMENT_EXPECTED}
        NYX_PLT_HOOK_HAS_BYTEHOOK=${NYX_PLT_HOOK_HAS_BYTEHOOK}
        NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER=${NYX_PLT_HOOK_USE_SHADOWHOOK_LINKER}
        NYX_VFS_CALLER_LIBRARY="${NYX_VFS_CALLER_LIBRARY}"
        NYX_VFS_OPEN_WRITE_SYMBOL="${NYX_VFS_OPEN_WRITE_SYMBOL}"
        NYX_VFS_OPENAT_WRITE_SYMBOL="${NYX_VFS_OPENAT_WRITE_SYMBOL}"
        NYX_VFS_STAT_SYMBOL="${NYX_VFS_STAT_SYMBOL}"
        NYX_VFS_LSTAT_SYMBOL="${NYX_VFS_LSTAT_SYMBOL}"
        NYX_VFS_FSTATAT_SYMBOL="${NYX_VFS_FSTATAT_SYMBOL}"
    )
endfunction()
