include_guard(GLOBAL)

# mod.properties 给默认值，Gradle/CMake -D 传入的同名变量优先
function(nyx_mod_option out_name file_path key)
    nyx_property_bool(default_value "${file_path}" "${key}" "false")
    if(DEFINED ${out_name})
        set(result ${${out_name}})
    else()
        set(result ${default_value})
    endif()
    set(${out_name} ${result} PARENT_SCOPE)
endfunction()

# 功能开关打开时必须已经迁移出真实源码，不允许只靠空壳通过编译
function(nyx_require_mod_source mod_dir feature source_path)
    if(NOT EXISTS "${mod_dir}/${source_path}")
        message(FATAL_ERROR "${feature} requires a real migrated implementation file: ${source_path}")
    endif()
endfunction()

# mod.cmake 可以写绝对路径、mod 内相对路径，也可以引用 cpp 根目录下的共享源码
function(nyx_resolve_mod_sources out_name source_dir mod_dir)
    set(resolved_sources)
    foreach(mod_source IN LISTS ARGN)
        if(IS_ABSOLUTE "${mod_source}")
            list(APPEND resolved_sources "${mod_source}")
        elseif(EXISTS "${mod_dir}/${mod_source}")
            list(APPEND resolved_sources "${mod_dir}/${mod_source}")
        else()
            list(APPEND resolved_sources "${source_dir}/${mod_source}")
        endif()
    endforeach()
    set(${out_name} ${resolved_sources} PARENT_SCOPE)
endfunction()

# 选择当前 active mod，并把 mod.properties / mod.cmake 收敛成后续 target 会用的变量
macro(nyx_configure_active_mod source_dir)
    # Gradle 构建会通过 -DNYX_TARGET_TAG 覆盖这里；这个默认值只服务直接调用 CMake 的场景。
    set(NYX_TARGET_TAG "demo" CACHE STRING "Active NyxCore mod")
    set(NYX_MOD_DIR "${source_dir}/mods/${NYX_TARGET_TAG}")
    set(NYX_MOD_PROPERTIES "${NYX_MOD_DIR}/mod.properties")
    set(NYX_MOD_CMAKE "${NYX_MOD_DIR}/mod.cmake")

    if(NOT EXISTS "${NYX_MOD_PROPERTIES}")
        message(FATAL_ERROR "Active mod properties missing: ${NYX_MOD_PROPERTIES}")
    endif()
    if(NOT EXISTS "${NYX_MOD_CMAKE}")
        message(FATAL_ERROR "Active mod CMake file missing: ${NYX_MOD_CMAKE}")
    endif()

    nyx_property(NYX_MOD_ID "${NYX_MOD_PROPERTIES}" "id" "")
    if(NOT NYX_MOD_ID STREQUAL NYX_TARGET_TAG)
        message(FATAL_ERROR "Active mod id '${NYX_MOD_ID}' does not match target tag '${NYX_TARGET_TAG}'")
    endif()

    # outputName 由 mod.properties 定义，mod.cmake 不能偷偷改最终 so 名
    nyx_required_property(NYX_MOD_OUTPUT_NAME_PROPERTY "${NYX_MOD_PROPERTIES}" "outputName")
    nyx_property(NYX_MOD_AUTH_PROVIDER "${NYX_MOD_PROPERTIES}" "auth" "none")
    string(TOLOWER "${NYX_MOD_AUTH_PROVIDER}" NYX_MOD_AUTH_PROVIDER)
    nyx_mod_option(NYX_MOD_ENABLE_UNITY_MAIN "${NYX_MOD_PROPERTIES}" "enableUnityMain")
    nyx_mod_option(NYX_MOD_ENABLE_UNITY_LOADER "${NYX_MOD_PROPERTIES}" "enableUnityLoader")
    nyx_mod_option(NYX_MOD_ENABLE_LIBRARY_REDIRECT "${NYX_MOD_PROPERTIES}" "enableLibraryRedirect")
    nyx_mod_option(NYX_MOD_ENABLE_ENCRYPTED_PAYLOAD "${NYX_MOD_PROPERTIES}" "enableEncryptedPayload")
    nyx_mod_option(NYX_MOD_ENABLE_VFS "${NYX_MOD_PROPERTIES}" "enableVfs")

    # include 前清掉输出变量，避免上一次配置残留影响当前 mod
    unset(NYX_MOD_OUTPUT_NAME)
    unset(NYX_MOD_SOURCES)
    unset(NYX_MOD_APPLY_TARGET_PROPERTIES)
    include("${NYX_MOD_CMAKE}")
    if(NOT DEFINED NYX_MOD_OUTPUT_NAME OR NYX_MOD_OUTPUT_NAME STREQUAL "")
        set(NYX_MOD_OUTPUT_NAME "${NYX_MOD_OUTPUT_NAME_PROPERTY}")
    endif()
    if(NOT NYX_MOD_OUTPUT_NAME STREQUAL NYX_MOD_OUTPUT_NAME_PROPERTY)
        message(FATAL_ERROR "mod.cmake output '${NYX_MOD_OUTPUT_NAME}' does not match mod.properties '${NYX_MOD_OUTPUT_NAME_PROPERTY}'")
    endif()

    nyx_resolve_mod_sources(NYX_MOD_RESOLVED_SOURCES "${source_dir}" "${NYX_MOD_DIR}" ${NYX_MOD_SOURCES})
    set(NYX_APP_TARGET "${NYX_TARGET_TAG}")
endmacro()

# 把当前 mod 配置统一注入目标：mod 身份、功能开关、鉴权和自定义宏
function(nyx_apply_mod_properties target_name)
    nyx_string_definition("${target_name}" NYX_TARGET_TAG "${NYX_TARGET_TAG}")
    nyx_string_definition("${target_name}" NYX_NATIVE_LIBRARY_NAME "lib${NYX_MOD_OUTPUT_NAME}.so")
    nyx_bool_definitions(
        "${target_name}"
        NYX_MOD_ENABLE_UNITY_MAIN
        NYX_MOD_ENABLE_UNITY_LOADER
        NYX_MOD_ENABLE_LIBRARY_REDIRECT
        NYX_MOD_ENABLE_ENCRYPTED_PAYLOAD
        NYX_MOD_ENABLE_VFS
    )

    # 目前真实鉴权 profile 只迁移了 WY；none 表示这个 mod 不注入鉴权后端
    if(NYX_MOD_AUTH_PROVIDER STREQUAL "wy")
        nyx_apply_wy_auth_properties("${target_name}" "${NYX_MOD_DIR}/config/auth.properties")
    elseif(NOT NYX_MOD_AUTH_PROVIDER STREQUAL "none")
        message(FATAL_ERROR "Unsupported mod auth provider: ${NYX_MOD_AUTH_PROVIDER}")
    endif()

    if(DEFINED NYX_MOD_APPLY_TARGET_PROPERTIES AND NOT NYX_MOD_APPLY_TARGET_PROPERTIES STREQUAL "")
        if(NOT COMMAND ${NYX_MOD_APPLY_TARGET_PROPERTIES})
            message(FATAL_ERROR "Active mod target hook is not a CMake command: ${NYX_MOD_APPLY_TARGET_PROPERTIES}")
        endif()
        cmake_language(CALL ${NYX_MOD_APPLY_TARGET_PROPERTIES} "${target_name}")
    endif()
endfunction()
