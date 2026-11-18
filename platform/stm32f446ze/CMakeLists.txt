# platform/stm32f446ze：NUCLEO-F446ZE 板级粘合
# ---------------------------------------------------------------------------
# Library
# 语法：add_Library(<name> [STATIC|SHARED|MODULE|INTERFACE])
# 创建一个叫 platform_stm32f446ze 的库，类型是 INTERFACE：
#                                          1.不会编译生成.a或.so文件。
#                                          2.只是一个属性合集，包含include路径、宏定义、编译选项等。
#                                          3.任何依赖它的target都会继承这些属性
#
# 语法：set(<variable> <value> ...)
# 用来定义变量，后面可以通过 ${<variable>} 引用该变量
# ---------------------------------------------------------------------------
add_library(platform_stm32f446ze INTERFACE)

set(STM32CUBE_F4_DIR
    ${PROJECT_SOURCE_DIR}/third_party/STM32CubeF4
)
set(STM32F4_CMSIS_DEVICE_DIR
    ${STM32CUBE_F4_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx
)
# ---------------------------------------------------------------------------
# CMSIS / device headers
# 语法：target_include_directories(<target> [INTERFACE|PUBLIC|PRIVATE] <dirs> ...)
# 给目标添加头文件路径。INTERFACE（表示这些路径只会传递给依赖它的目标，因为库本身不编译）
# ---------------------------------------------------------------------------

target_include_directories(
    platform_stm32f446ze
    INTERFACE

    ${CMAKE_CURRENT_SOURCE_DIR}

    ${STM32CUBE_F4_DIR}/Drivers/CMSIS/Include
    ${STM32F4_CMSIS_DEVICE_DIR}/Include
)
# ---------------------------------------------------------------------------
# MCU selection / board clock facts
# 语法：target_compile_definitions(<target> [INTERFACE|PUBLIC|PRIVATE] <definitions> ...)
# 给目标添加编译宏，相当于 -D。INTERFACE（同理）
# ---------------------------------------------------------------------------

target_compile_definitions(
    platform_stm32f446ze
    INTERFACE

    STM32F446xx
    HSE_VALUE=8000000U
)
# ---------------------------------------------------------------------------
# Startup / CMSIS system source
# 语法：target_sources(<target> [INTERFACE|PUBLIC|PRIVATE] <sources> ...)
# 给目标添加源码文件。INTERFACE（同理）
# ---------------------------------------------------------------------------

target_sources(
    platform_stm32f446ze
    INTERFACE

    ${STM32F4_CMSIS_DEVICE_DIR}/Source/Templates/system_stm32f4xx.c
    ${STM32F4_CMSIS_DEVICE_DIR}/Source/Templates/gcc/startup_stm32f446xx.s
)
# ---------------------------------------------------------------------------
# Cortex-M4F architecture
# 语法：target_compile_options(<target> [INTERFACE|PUBLIC|PRIVATE] <options>...)
# 给目标添加编译器选项。INTERFACE（同理）
#
# 语法：target_link_options(<target> [INTERFACE|PUBLIC|PRIVATE] <options>...)
# 给目标添加链接器选项。INTERFACE（同理）
# ---------------------------------------------------------------------------

target_compile_options(
    platform_stm32f446ze
    INTERFACE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard

    # -ffunction-sections / -fdata-sections：把每个函数、每个数据对象各自放进
    #   独立的 section，配合下面的 -Wl,--gc-sections 才能回收未被引用的部分。
    #   少了这一对，--gc-sections 只能按目标文件的粗粒度回收。
    -ffunction-sections
    -fdata-sections
)

# ---------------------------------------------------------------------------
# 编译告警策略（E1.1）
# 语法：target_link_libraries(<target> INTERFACE <lib>...)
# embedded_warnings 定义在 cmake/warnings.cmake（项目级、与板无关）。
# 挂在平台上的作用是"继承"：任何链接 platform_stm32f446ze 的 target 都自动
# 得到 -Wall -Wextra -Wpedantic，实验侧不需要额外写一行，也不会漏配。
# ---------------------------------------------------------------------------
target_link_libraries(
    platform_stm32f446ze
    INTERFACE
    embedded_warnings
)

target_link_options(
    platform_stm32f446ze
    INTERFACE

    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard

    # nano.specs：newlib 精简版，裸机代码尺寸小得多。
    # nosys.specs：为 _read/_write/_close/_lseek/_fstat/_isatty/_getpid/_kill
    #             等 syscall 提供"永远失败"的空实现，让 libc 能链接通过。
    #
    # 已知副作用：每次链接都会打印 4 条警告，形如
    #   warning: _close is not implemented and will always fail
    # 这些是【链接期】警告，-Wall/-Wextra/-Wpedantic 对它们完全无效。
    # 它们也是【虚警】：紧跟的 note 已说明
    #   "the message above does not take linker garbage collection into account"
    # 即警告发出时链接器还没执行下面的 -Wl,--gc-sections；本项目从不调用
    # 这些 stub，它们会被回收掉、不进固件（可在 E1.2 的 .map 里印证）。
    #
    # 真正的解法是自己实现 syscall（尤其 _write 重定向到 UART 做 printf），
    # 那属于后续 UART 实验的范围。届时这 4 条会自然消失，不要用链接选项压制——
    # 压制会同时盖掉真实的 undefined symbol。
    --specs=nano.specs
    --specs=nosys.specs

    -Wl,--gc-sections
)
# ---------------------------------------------------------------------------
# Linker script
# ---------------------------------------------------------------------------

set(STM32F446ZE_LINKER_SCRIPT
    ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F446ZE.ld
)

target_link_options(
    platform_stm32f446ze
    INTERFACE

    -T${STM32F446ZE_LINKER_SCRIPT}
)

# 让所有链接该平台的 target 自动继承对 linker script 的依赖追踪
set_target_properties(
    platform_stm32f446ze
    PROPERTIES
    
    INTERFACE_LINK_DEPENDS "${STM32F446ZE_LINKER_SCRIPT}"
)
# ---------------------------------------------------------------------------
# 一键烧录：flash_<target>
#
# 语法：find_program(<VAR> <name> [DOC <doc>]) 在 PATH 中查找可执行文件
# 语法：add_custom_target(<name> [COMMAND ...] [DEPENDS ...] [VERBATIM]
#                         [USES_TERMINAL] [COMMENT ...])
# 语法：CMAKE_CURRENT_FUNCTION_LIST_DIR —— 函数"定义处"的目录，
#       即使从别的目录调用也能定位到本板的 openocd.cfg。
#
# 边界：
#   - 板级知识（调试器、传输方式、芯片）只在本目录：openocd.cfg + 本函数。
#   - 实验只写 stm32f446ze_add_flash_target(<target>)，不出现 openocd 路径、
#     不出现固件路径。
#   - 烧录逻辑只有一份：scripts/flash.sh。CMake 只负责把参数传进去，
#     以后加 verify / 重试 / 日志 / CI 调用都改脚本，不动 CMake。
#   - Windows 上 bash 由 MSYS2 / Git Bash 提供；脚本强制 LF 行尾，
#     规则在仓库根 .gitattributes。
# ---------------------------------------------------------------------------

find_program(
    BASH_EXECUTABLE
    bash
    DOC "bash used by flash_<target> to run scripts/flash.sh"
)

function(stm32f446ze_add_flash_target target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR
            "stm32f446ze_add_flash_target: target '${target_name}' does not exist"
        )
    endif()

    set(flash_target "flash_${target_name}")

    if(TARGET ${flash_target})
        message(FATAL_ERROR
            "stm32f446ze_add_flash_target: target '${flash_target}' already exists"
        )
    endif()

    if(BASH_EXECUTABLE)
        add_custom_target(
            ${flash_target}

            COMMAND
                "${BASH_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/flash.sh"
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/openocd.cfg"
                "$<TARGET_FILE:${target_name}>"

            DEPENDS
                ${target_name}

            USES_TERMINAL
            VERBATIM
            COMMENT "flash ${target_name}"
        )
    else()
        add_custom_target(
            ${flash_target}
            COMMAND ${CMAKE_COMMAND} -E echo
                    "bash not found: install Git Bash or MSYS2, then re-run CMake"
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
            COMMENT "flash ${target_name}: bash not found"
        )
    endif()
endfunction()
