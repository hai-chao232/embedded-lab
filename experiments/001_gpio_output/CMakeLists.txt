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

# 烧录入口：ninja -C build flash_exp001_gpio_output
# 只声明"这个 target 要能一键烧录"，调试器品牌与固件路径由平台层负责
stm32f446ze_add_flash_target(exp001_gpio_output)

