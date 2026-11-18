# platform：板级粘合层（每块板一份）
add_subdirectory(stm32f446ze)

# 板级契约：每个 platform/<board>/ 目录提供
#   1. <board>_add_flash_target(<target>)  —— 生成 flash_<target> 目标
#   2. openocd.cfg                        —— 该板的调试器 + 目标芯片配置
# 实验侧只调用 <board>_add_flash_target()，烧录细节留在板级目录里。
