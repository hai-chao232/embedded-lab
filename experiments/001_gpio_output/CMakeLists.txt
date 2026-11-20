add_executable(
    exp001_gpio_output
    main.c
)

target_link_libraries(
    exp001_gpio_output
    PRIVATE
    platform_stm32f446ze
)

set_target_properties(
    exp001_gpio_output
    PROPERTIES
    OUTPUT_NAME "exp001_gpio_output"
    SUFFIX ".elf"
)

# 烧录入口：ninja -C build/debug flash_exp001_gpio_output（E1.3 起分 debug/release 目录）
# 只声明"这个 target 要能一键烧录"，调试器品牌与固件路径由平台层负责
stm32f446ze_add_flash_target(exp001_gpio_output)

# 固件产物（E1.2）：.map / .bin / .hex / size，策略在项目层 cmake/firmware.cmake。
# 新建固件实验必须带这一行（POST_BUILD 无法经 platform 自动继承）。
embedded_add_firmware_artifacts(exp001_gpio_output)

