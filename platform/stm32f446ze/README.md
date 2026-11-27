# platform/stm32f446ze/ 板级粘合层

> 上级：[platform/](../README.md)

**每块板只有一份**，所有实验共享。定位：vendor 代码（third_party）与实验之间的粘合。
（平台层的三层边界见上级 [platform/README.md](../README.md)，此处不重复。）

## 规划内容

| 模块 | 说明 | 状态 |
|------|------|:----:|
| clock | ST-LINK MCO 8 MHz → **HSE bypass** → PLL → SYSCLK 180 MHz（板载无独立 HSE 晶振），AHB /1、APB1 /4、APB2 /2 | ✅ `clock/board_clock.c` |
| startup | 从 CubeF4 选 startup_stm32f446xx.s（gcc 版） | ✅ |
| irq | NVIC 配置、中断向量管理（内核部分知识归 `docs/cortex_m/`） | ⬜ |
| board | LED/按键引脚定义等板级封装 | ⬜ |
| linker script | STM32F446ZE 内存布局（512K Flash / 128K SRAM） | ✅ |
| printf 重定向 | retarget 到板载 ST-Link 虚拟串口（USART3，PD8/PD9） | ⬜ 属 011 UART 实验 |

## 调试连接注意

003/004 进入 `__WFI()` 后 CPU 会睡眠。`stm32f4x.cfg` 的 `DBGMCU_CR.DBG_SLEEP` 是在 examine-end 才置位的；如果目标已经在 sleep 中，直接 examine 可能失败（`Examination failed` / `reset will not halt`）。因此 `openocd.cfg` 配置了 `connect_assert_srst`：连接时先拉低 SRST，examine 完成后再释放。若换用没有把 SRST 接到目标的调试器/板子，这一行需要重新评估。

## clock 模块（Clock checkpoint）

`clock/board_clock.h` + `clock/board_clock.c`，所有实验共享一份。头文件是时钟树的**唯一来源**：

```c
board_clock_status_t s = board_clock_init();   // HSE bypass 8 MHz -> PLL -> 180 MHz
/* 之后：board_clock_hclk_hz() / pclk1_hz() / pclk2_hz()
        board_clock_apb1_timer_hz() / apb2_timer_hz() */
```

| 项 | 值 | 备注 |
|---|---:|---|
| HSE | 8 MHz | ST-LINK MCO，**bypass**；板上无晶振，不能按 crystal 模式起振 |
| PLL | M=4 N=180 P=2 Q=7 | VCO 输入 2 MHz（推荐值），VCO 360 MHz，SYSCLK 180 MHz |
| SYSCLK / HCLK | 180 MHz | AHB /1 |
| PCLK1 / TIM7CLK | 45 / 90 MHz | APB1 /4，定时器时钟 ×2 |
| PCLK2 / APB2 TIMx | 90 / 180 MHz | APB2 /2 |
| Flash | 5 WS + PRFTEN + ICEN + DCEN | 150–180 MHz 在 VOS1 下要 5 个等待周期 |

边界与坑：

- **`SystemCoreClock` 是 HCLK**，不是 TIM 时钟。算 TIM7 / 006 PWM 用 `board_clock_apb1_timer_hz()`；
  ×2 规则只在 APB 分频 ≠1 时成立，APB /1 时定时器就是 PCLK 本身。
- **F446 的 `VOS` 只在 PLL 打开后生效**：PLL 关闭时稳压器被强制在 scale 3，`PWR_CSR.VOSRDY` 一直是 0。
  正确顺序是 写 `VOS=11` → 开 PLL → 等 `PLLRDY` → 等 `VOSRDY`；先等 `VOSRDY` 会超时。
  这条约束写在 HAL `stm32f4xx_hal_pwr_ex.c` 的注释里，005 第一次上板就是踩在它上面。
- **`PLLQ=7` 得 51.4 MHz ≠ 48 MHz**：USB FS 要另开 PLLSAI，留给 022。
- 初始化失败返回 `board_clock_status_t` 枚举，不死循环：调用方决定怎么暴露（005 用 LD3 常亮 + `clock_status`）。
- 幂等：先切回 HSI、关 PLL 再重建，所以可以从复位状态调用，也可以从 180 MHz 状态再调用一次。
- 001–004 不调用它，仍跑复位默认的 HSI 16 MHz（它们的忙等和 SysTick 都按 16 MHz 写的）；
  时钟升级从 005 开始，这是有意的分界。
- 用 `_Static_assert` 把 RM0390 的约束（VCO 输入 1–2 MHz、VCO 100–432 MHz、SYSCLK ≤180 MHz、PLLN/PLLQ
  范围、5 WS）变成编译期检查：改一个因子，整棵树和所有实验一起重算或一起编译失败。
