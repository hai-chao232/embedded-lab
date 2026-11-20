# common：与硬件、RTOS 均无关的公共件
# 现阶段为空壳 INTERFACE 库；组件随实验需求逐步上提
add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# common/ 的源码也受项目告警基线约束。
# 前提：根 CMakeLists.txt 中 include(cmake/warnings.cmake) 必须在add_subdirectory(common) 之前
target_link_libraries(common INTERFACE embedded_warnings)
