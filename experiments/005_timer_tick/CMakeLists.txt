add_executable(
    exp005_timer_tick
    main.c
)

target_link_libraries(
    exp005_timer_tick
    PRIVATE
    platform_stm32f446ze
)

set_target_properties(
    exp005_timer_tick
    PROPERTIES
    OUTPUT_NAME "exp005_timer_tick"
    SUFFIX ".elf"
)

# 烧录入口：ninja -C build/debug flash_exp005_timer_tick（E1.3 起分 debug/release 目录）
# 只声明"这个 target 要能一键烧录"，调试器品牌与固件路径由平台层负责
stm32f446ze_add_flash_target(exp005_timer_tick)

# 固件产物（E1.2）：.map / .bin / .hex / size，策略在项目层 cmake/firmware.cmake。
# 新建固件实验必须带这一行（POST_BUILD 无法经 platform 自动继承）。
embedded_add_firmware_artifacts(exp005_timer_tick)
