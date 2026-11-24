# platform/stm32f446ze/ 板级粘合层

> 上级：[platform/](../README.md)

**每块板只有一份**，所有实验共享。定位：vendor 代码（third_party）与实验之间的粘合。
（平台层的三层边界见上级 [platform/README.md](../README.md)，此处不重复。）

## 规划内容

| 模块 | 说明 | 状态 |
|------|------|:----:|
| clock | ST-LINK MCO 8 MHz → **HSE bypass** → PLL → SYSCLK 180 MHz（板载无独立 HSE 晶振） | ⬜ 暂用 system_stm32f4xx.c 默认 HSI 16 MHz |
| startup | 从 CubeF4 选 startup_stm32f446xx.s（gcc 版） | ✅ |
| irq | NVIC 配置、中断向量管理（内核部分知识归 `docs/cortex_m/`） | ⬜ |
| board | LED/按键引脚定义等板级封装 | ⬜ |
| linker script | STM32F446ZE 内存布局（512K Flash / 128K SRAM） | ✅ |
| printf 重定向 | retarget 到板载 ST-Link 虚拟串口（USART3，PD8/PD9） | ⬜ 属 011 UART 实验 |

## 调试连接注意

003/004 进入 `__WFI()` 后 CPU 会睡眠。`stm32f4x.cfg` 的 `DBGMCU_CR.DBG_SLEEP` 是在 examine-end 才置位的；如果目标已经在 sleep 中，直接 examine 可能失败（`Examination failed` / `reset will not halt`）。因此 `openocd.cfg` 配置了 `connect_assert_srst`：连接时先拉低 SRST，examine 完成后再释放。若换用没有把 SRST 接到目标的调试器/板子，这一行需要重新评估。
