# ARM Cortex-M 交叉编译工具链定义
# 用法: cmake --preset stm32f446ze-debug（或 -release；presets 定义见 CMakePresets.json）

set(CMAKE_SYSTEM_NAME Generic)      # 裸机，无 OS
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

# 编译/汇编共用 GCC 前端
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

# 二进制工具：configure 时用 find_program 解析成绝对路径。
# 裸名字会把问题推迟到构建期靠 PATH 碰运气（IDE / CI 环境 PATH 漂移时晚爆）；
# find_program 结果入 cache，之后不再受 PATH 变化影响。
# 与 E1.0 的 find_program(BASH_EXECUTABLE) 是同一模式。
#
# 迁移清理（坑，只对"已有构建目录"成立）：
#   本文件旧版用 set(... CACHE FILEPATH) 存的是裸名字。find_program 发现
#   变量已有值（含 cache 值）就视为"已有结果"跳过搜索——旧 cache 里的裸名字
#   会被原样保留，永远不解析。全新 build 目录没有旧值，find_program 正常搜索。
#   所以这里先把"不是绝对路径"的旧值清掉再查找；用户 -D 指定的绝对路径
#   会被 find_program 尊重，不做 FORCE 覆写。
foreach(_binutil IN ITEMS CMAKE_OBJCOPY CMAKE_OBJDUMP CMAKE_SIZE CMAKE_GDB)
    if(DEFINED CACHE{${_binutil}} AND NOT IS_ABSOLUTE "$CACHE{${_binutil}}")
        unset(${_binutil} CACHE)
    endif()
endforeach()

find_program(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy DOC "objcopy")
find_program(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump DOC "objdump")
find_program(CMAKE_SIZE    ${TOOLCHAIN_PREFIX}size    DOC "size")
find_program(CMAKE_GDB     ${TOOLCHAIN_PREFIX}gdb     DOC "gdb")

# 按用途分级校验：
#   objcopy / size —— 构建硬依赖（E1.2 的 POST_BUILD 用到），缺失即失败；
#   objdump / gdb  —— 只用于分析/调试，不参与构建与烧录，缺失仅警告，
#                     精简工具链（不带 gdb）不应被挡在 configure 之外。
foreach(_binutil IN ITEMS CMAKE_OBJCOPY CMAKE_SIZE)
    if(NOT ${_binutil})
        message(FATAL_ERROR
            "arm-none-eabi toolchain: ${_binutil} not found in PATH; "
            "install GNU Arm Toolchain and ensure its bin/ is on PATH")
    endif()
endforeach()
foreach(_binutil IN ITEMS CMAKE_OBJDUMP CMAKE_GDB)
    if(NOT ${_binutil})
        message(WARNING
            "arm-none-eabi toolchain: ${_binutil} not found in PATH; "
            "build is unaffected, but analysis/debugging with it will be unavailable")
    endif()
endforeach()

# 交叉编译时禁止运行探测程序（目标机跑不了 host 程序）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 只在目标系统根目录里找库/头文件；host 工具（如 cmake 自身）不受影响
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
