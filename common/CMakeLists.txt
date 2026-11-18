# common：与硬件、RTOS 均无关的公共件
# 现阶段为空壳 INTERFACE 库；组件随实验需求逐步上提
add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# 让 common/ 的源码也受项目告警基线约束。
# 前提：根 CMakeLists.txt 中 include(cmake/warnings.cmake) 必须在
# add_subdirectory(common) 之前 —— 目前顺序已满足，勿调换。
# 现在是空壳库、不编译任何源文件，所以这行暂时不产生实际效果；
# 等 common/ 长出 ring buffer 等真实源码时，它们不会静默漏在基线之外。
target_link_libraries(common INTERFACE embedded_warnings)
