if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Ninja")
    return()
endif()

# 直接保留编译器输出字节，避免旧 CMake 按系统代码页误解 /showIncludes 前缀。
# 只包含本地生成头，不依赖 SDK 搜索路径，也不把任何项目源码传给外部程序。
set(_florrbt_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/florrbt-include-probe")
file(MAKE_DIRECTORY "${_florrbt_probe_dir}")
file(WRITE "${_florrbt_probe_dir}/florrbt_include_probe.h" "#pragma once\n")
file(WRITE "${_florrbt_probe_dir}/probe.cpp" "#include \"florrbt_include_probe.h\"\n")
execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" /nologo /utf-8 /Zs /showIncludes "${_florrbt_probe_dir}/probe.cpp"
    WORKING_DIRECTORY "${_florrbt_probe_dir}"
    OUTPUT_VARIABLE _florrbt_probe_output
    ERROR_VARIABLE _florrbt_probe_error
    RESULT_VARIABLE _florrbt_probe_result
    ENCODING NONE
    TIMEOUT 10
)
if(_florrbt_probe_result EQUAL 0)
    string(REPLACE "\r" "" _florrbt_probe_output "${_florrbt_probe_output}\n${_florrbt_probe_error}")
    string(REPLACE "\n" ";" _florrbt_probe_lines "${_florrbt_probe_output}")
    foreach(_florrbt_probe_line IN LISTS _florrbt_probe_lines)
        if(_florrbt_probe_line MATCHES "florrbt_include_probe[.]h")
            # Windows 路径的盘符冒号后没有空格，前面的本地化提示以冒号和空格结束。
            string(REGEX MATCH "^.+: +" _florrbt_include_prefix "${_florrbt_probe_line}")
            if(_florrbt_include_prefix)
                set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_florrbt_include_prefix}")
                set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${_florrbt_include_prefix}")
                set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_florrbt_include_prefix}")
                break()
            endif()
        endif()
    endforeach()
endif()
if(NOT _florrbt_include_prefix)
    message(WARNING "Unable to probe MSVC include prefix; retaining the CMake compiler detection result")
endif()
unset(_florrbt_probe_dir)
unset(_florrbt_probe_output)
unset(_florrbt_probe_error)
unset(_florrbt_probe_result)
unset(_florrbt_probe_lines)
unset(_florrbt_probe_line)
unset(_florrbt_include_prefix)
