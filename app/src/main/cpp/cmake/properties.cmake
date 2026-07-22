include_guard(GLOBAL)

# 读取简单 key=value 配置；空行、# 和 ! 开头的行会跳过
function(nyx_property out_name file_path key default_value)
    if(NOT EXISTS "${file_path}")
        message(FATAL_ERROR "Properties file missing: ${file_path}")
    endif()

    set(result "${default_value}")
    file(STRINGS "${file_path}" lines ENCODING UTF-8)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line STREQUAL "" OR line MATCHES "^[#!]")
            continue()
        endif()

        # 这里只支持项目当前使用的朴素格式，不处理 Java properties 的转义语义
        string(FIND "${line}" "=" split)
        if(split LESS 0)
            continue()
        endif()

        string(SUBSTRING "${line}" 0 ${split} prop_key)
        math(EXPR value_start "${split} + 1")
        string(SUBSTRING "${line}" ${value_start} -1 prop_value)
        string(STRIP "${prop_key}" prop_key)
        string(STRIP "${prop_value}" prop_value)

        if(prop_key STREQUAL "${key}")
            set(result "${prop_value}")
        endif()
    endforeach()

    set(${out_name} "${result}" PARENT_SCOPE)
endfunction()

# 可选配置文件读取：文件不存在时直接返回默认值
function(nyx_optional_property out_name file_path key default_value)
    if(EXISTS "${file_path}")
        nyx_property(result "${file_path}" "${key}" "${default_value}")
    else()
        set(result "${default_value}")
    endif()
    set(${out_name} "${result}" PARENT_SCOPE)
endfunction()

# 必填字段为空时尽早失败，避免编译出缺配置的 so
function(nyx_required_property out_name file_path key)
    nyx_property(value "${file_path}" "${key}" "")
    if(value STREQUAL "")
        message(FATAL_ERROR "Required property '${key}' is empty in ${file_path}")
    endif()
    set(${out_name} "${value}" PARENT_SCOPE)
endfunction()

# 把配置文本转成 CMake bool，允许常见 true/false 写法
function(nyx_bool_from_text out_name value)
    string(TOLOWER "${value}" normalized)
    if(normalized STREQUAL "true" OR normalized STREQUAL "on" OR normalized STREQUAL "yes" OR normalized STREQUAL "1")
        set(result ON)
    elseif(normalized STREQUAL "false" OR normalized STREQUAL "off" OR normalized STREQUAL "no" OR normalized STREQUAL "0" OR normalized STREQUAL "")
        set(result OFF)
    else()
        message(FATAL_ERROR "Invalid boolean value: ${value}")
    endif()
    set(${out_name} ${result} PARENT_SCOPE)
endfunction()

# 从 properties 里读取 bool 字段
function(nyx_property_bool out_name file_path key default_value)
    nyx_property(value "${file_path}" "${key}" "${default_value}")
    nyx_bool_from_text(result "${value}")
    set(${out_name} ${result} PARENT_SCOPE)
endfunction()

# 转成 C/C++ 字符串字面量前先转义反斜杠和双引号
function(nyx_cpp_string out_name value)
    set(result "${value}")
    string(REPLACE "\\" "\\\\" result "${result}")
    string(REPLACE "\"" "\\\"" result "${result}")
    set(${out_name} "${result}" PARENT_SCOPE)
endfunction()

# 注入字符串宏，例如 NAME="value"
function(nyx_string_definition target_name name value)
    nyx_cpp_string(escaped "${value}")
    target_compile_definitions("${target_name}" PRIVATE "${name}=\"${escaped}\"")
endfunction()

# 注入十进制整数宏，配置写错时直接失败
function(nyx_int_definition target_name name value)
    if(NOT "${value}" MATCHES "^-?[0-9]+$")
        message(FATAL_ERROR "Integer definition ${name} has invalid value: ${value}")
    endif()
    target_compile_definitions("${target_name}" PRIVATE "${name}=${value}")
endfunction()

# 注入十进制或 0x 十六进制数值宏，给地址/大小这类字段使用
function(nyx_numeric_definition target_name name value)
    if(NOT "${value}" MATCHES "^(0[xX][0-9A-Fa-f]+|[0-9]+)$")
        message(FATAL_ERROR "Numeric definition ${name} has invalid value: ${value}")
    endif()
    target_compile_definitions("${target_name}" PRIVATE "${name}=${value}")
endfunction()
