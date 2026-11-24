cmake_minimum_required(VERSION 3.21)

project(embedded-lab C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 项目级编译告警策略（E1.1）：定义 embedded_warnings，由 platform 传递给所有实验。
# 必须在 add_subdirectory 之前 include，后面的目录才能链接它。
include(cmake/warnings.cmake)

# 项目级固件产物策略（E1.2）：定义 embedded_add_firmware_artifacts，
# 由各实验显式调用（POST_BUILD 无法经 INTERFACE 库继承，见该文件注释）。
include(cmake/firmware.cmake)

# 项目级 Debug / Release 构建模型（E1.3）：默认 Debug，显式定义各类型的
# 优化/调试语义；同样必须在 add_subdirectory 之前，子目录才能继承标志。
include(cmake/build_type.cmake)

# 公共设施与板级平台：所有实验共享，只有一份
add_subdirectory(common)
add_subdirectory(platform)

# 实验登记：显式列表，不用 glob；新增实验在这里加一行
set(EXPERIMENTS
  001_gpio_output
  002_gpio_input
  003_exti
  004_nvic
)

foreach(exp IN LISTS EXPERIMENTS)
  add_subdirectory(experiments/${exp})
endforeach()
