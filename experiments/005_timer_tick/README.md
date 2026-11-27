# 实验 005：timer 基本定时中断

> **前置：Clock checkpoint**（见 [ROADMAP](../../ROADMAP.md#L110-L122)）。
> 本 README 按目标时钟写：HSE bypass 8 MHz → PLL → SYSCLK 180 MHz；
> APB1 /4 = 45 MHz，TIM7 时钟 = 90 MHz。
> Clock checkpoint 已落地在 [`platform/stm32f446ze/clock/`](../../platform/stm32f446ze/clock/board_clock.c)：
> 005 的 `main()` 第一行调用 `board_clock_init()`，下面的周期计算是实测过的，不是设计稿。

## 现象

黑盒行为规格：只描述外部可观察的行为，是写代码前的契约、烧录后的验收清单。

1. **初始态**：上电复位后 LD1 熄灭；时钟 / TIM7 初始化完成后，LD1 以 **1 Hz** 闪烁（500 ms 亮、500 ms 灭）。
2. **周期稳定**：1 s 周期由 TIM7 的更新中断驱动；主循环在 `__WFI()` 睡眠，没有轮询、没有软件 delay。用逻辑分析仪测 PB0，周期应为 1 s。
3. **时钟验收（验证变体）**：把 `PSC` / `ARR` 改掉并重新烧录后，LD1 周期按比例变化；默认是 1 ms tick + 500 分频 = 1 Hz。
4. **故障注入（可选）**：在 TIM7 ISR 里人为加入 >1 ms 的忙等，1 ms tick 会丢，LD1 周期变长。这说明 1 ms 是预算，不是“中断一定准时”的保证。

第 1–2 条必须成立；第 3 条是 Clock checkpoint 的验收；第 4 条是可选故障注入。

## 目的

核心一句话：**005 给系统装上第一根“自己走的秒针”。**

001–004 都是“对外部事件或内核 tick 做反应”：写输出、读输入、响应边沿、处理抢占。005 第一次让 MCU **自己产生时间**，不依赖按键、不依赖外部世界：

```text
RCC → PCLK1 → TIM7CLK → PSC → CNT → ARR → UEV → UIF → NVIC → ISR
```

这不是“再点一次灯”，而是把“时间”变成软件可以依赖的资源。后面的 PWM、输入捕获、ADC 触发、DMA 请求、UART 波特率、RTOS tick，全部以这条链为前提。

### 为什么用 basic timer TIM7，而不是 TIM2

- **005 只讲纯时基**：TIM7 是 basic timer，没有输出通道、没有输入捕获；认知点可以只落在 `PSC / ARR / CNT / UEV / UIF / DIER / NVIC / ISR`。
- **006 再上带通道的通用定时器**：PWM 需要 `CCMR / CCR / CCER`；005 不把“时基”和“波形”混在一起，006 才能在同一个时基模型上只加通道。
- **为什么选 TIM7 而不是 TIM6**：两者都是 basic timer；TIM6 的中断和 DAC 共用 `TIM6_DAC_IRQn`，TIM7 有独立的 `TIM7_IRQn`，005 不需要 DAC 干扰。

### SysTick 与 TIM7 的分工

| | SysTick（004 用过） | TIM7（005） |
|---|---|---|
| 类型 | Cortex-M4 内核 | APB1 basic timer |
| 位宽 | 24 位向下 | 16 位向上 |
| 预分频 | /1 或 /8 | 16 位 `PSC` |
| 通道 | 无 | 无（basic timer） |
| 时钟 | HCLK | PCLK1 × 2 = 90 MHz |
| 典型用途 | OS tick、简单延时 | 纯时基 |

005 用 TIM7，是为了把“内核 tick”换成“外设定时器”，同时不提前引入通道。

三层认知增量，每层挂一个可观察的验证：

- **外设/寄存器层**：把 TIM7 的时基链路走通。
  - `RCC->APB1ENR.TIM7EN` 开时钟；
  - `PSC` 分频、`ARR` 重装、`CNT` 计数；
  - `EGR.UG` 让 `PSC/ARR` 立即生效，同时会置 `UIF`；
  - `DIER.UIE` 开更新中断，`CR1.CEN` 启动；
  - `SR.UIF` 在 ISR 里写 0 清除；
  - `NVIC_EnableIRQ(TIM7_IRQn)`。
  - 验证：调试器 watch `TIM7->CNT`、`TIM7->SR`、`ms_ticks`。
- **架构层**：定时中断 vs 软件 delay/轮询。
  - 主循环 `while (1) { __WFI(); }`，CPU 睡眠，1 ms 中断唤醒；
  - ISR 只做最小事：清 `UIF`、`ms_ticks++`、500 分频翻转 LD1；
  - 1 ms 是预算：ISR 超过 1 ms 就会丢 tick，006 会用硬件输出比较把“每次翻转”从 CPU 手里拿走。
  - 验证：用 LA 看 PB0 的 1 s 周期；人为拖长 ISR 看是否丢 tick。
- **时钟/物理层**：005 是 Clock checkpoint 的验收。
  - HSE bypass 8 MHz（ST-LINK MCO，板载无独立晶振）→ PLL → 180 MHz；
  - `SystemCoreClock` = HCLK = 180 MHz，**不是** TIM7 的 90 MHz；
  - APB1 /4 = 45 MHz，但 APB1 定时器时钟 ×2 = 90 MHz；
  - LED 的 1 s 周期是“时钟树 + PSC/ARR”共同算出来的结果。
  - 验证：周期成 2×/4× 整数倍偏差，先查 APB1 预分频和 APB 定时器 ×2；差 1% 左右是 HSI/HSE 精度。

刻意不做：不做 PWM（006）、不做输入捕获（007）、不做 DMA/ADC 触发（008–010）、不做 RTOS tick（第三阶段）、不做消抖（005 之后的 Debounce checkpoint）、不做优先级实验（004 已讲）。单实验单认知点。

工程线：005 继续复用 E1 的构建/烧录入口；新增的只有 platform clock 模块和 TIM7 初始化。

## 时钟基线

Clock checkpoint 完成后的目标（建议 APB1 /4、APB2 /2；若最终选别的分频，按同一公式重算 TIM7CLK）：

| 时钟 | 值 | 来源 |
|---|---:|---|
| HCLK / `SystemCoreClock` | 180 MHz | AHB /1 |
| PCLK1 | 45 MHz | APB1 /4 |
| TIM7CLK | 90 MHz | APB1 预分频 ≠1，timer clock ×2 |
| PCLK2 | 90 MHz | APB2 /2 |

计算：

```text
f_TIM7  = PCLK1 × 2            = 90 MHz
f_UPDATE = f_TIM7 / ((PSC+1)(ARR+1))
         = 90 MHz / 90 / 1000  = 1 kHz   → 1 ms
```

本实验取值：`PSC = 89`、`ARR = 999`。

平台实现取 `PLLM = 4`、`PLLN = 180`、`PLLP = 2`、`PLLQ = 7`：VCO 输入 8/4 = 2 MHz（RM0390 的推荐值，
比 1 MHz 抖动更小），VCO 360 MHz，SYSCLK = 360/2 = 180 MHz。`PLLQ = 7` 得 51.4 MHz、不是 48 MHz——
F446 的 USB FS 时钟要另开 PLLSAI，属于 022 的范围，005 不解决。

几个关键点：

- **`SystemCoreClock` 不是 TIM7CLK**：前者是 HCLK 180 MHz，后者是 PCLK1×2 = 90 MHz。不要拿 `SystemCoreClock` 直接算 TIM7。
- **×2 规则只在 APB 预分频 ≠1 时生效**：180 MHz 下 APB1 /4，所以 TIM7CLK = 45 MHz × 2 = 90 MHz；如果 APB1 是 /1，定时器时钟就是 PCLK1 本身。
- **TIM7 是 16 位**：`PSC=89`、`ARR=999` 都放得下；500 ms 的 LED 翻转用软件 500 分频，不需要 32 位 ARR。
- **平台层负责时钟**：180 MHz 的 HSE/PLL/Flash latency/APB 分频应放在 `platform/stm32f446ze/` 的 clock 模块，005 不重复写 PLL；否则 006 之后每个实验都要复制一份。
- **005 是 Clock checkpoint 的验收**：Clock checkpoint 本身可以用 MCO/LA 验证 SYSCLK；005 的 1 s LED 周期是二级验收。周期对不上，先查时钟树，不要先怀疑 TIM7 代码。

## 关键寄存器与操作

| 寄存器/位或操作 | 作用 | 本实验设置 |
|---|---|---|
| `RCC->APB1ENR.TIM7EN` | TIM7 时钟 | 1 |
| `TIM7->PSC` | 预分频 | 89 |
| `TIM7->ARR` | 自动重装 | 999 |
| `TIM7->CNT` | 当前计数 | 0 |
| `TIM7->EGR.UG` | 立即装载 PSC/ARR；会置 UIF | 1 |
| `TIM7->SR.UIF` | 更新标志 | 写 0 清除 |
| `TIM7->DIER.UIE` | 更新中断使能 | 1 |
| `TIM7->CR1.CEN` | 计数器使能 | 1 |
| `NVIC_EnableIRQ(TIM7_IRQn)` | 使能 IRQ55 | ISER1 bit23 |
| `__WFI()` | 睡眠等待中断 | 主循环 |
| `ms_ticks` | 1 ms 软件 tick | `volatile uint32_t` |
| `PWR->CR.VOS` / `PWR_CSR.VOSRDY` | 稳压器 scale 1（**平台层**） | `11`；PLL 打开后才 ready |
| `FLASH->ACR` | 5 wait states + prefetch / I/D cache（平台层） | `0x705` |
| `RCC->PLLCFGR` | PLL M/N/P/Q（平台层） | M=4 / N=180 / P=2 / Q=7 |
| `RCC->CFGR` | SYSCLK 源 + AHB/APB 分频（平台层） | SW=SWS=PLL；/1、/4、/2 |

几个容易踩的坑：

- **`UIF` 是 rc_w0**：写 0 清除，写 1 无效；ISR 里不清就会反复进。
- **`UG` 会置 `UIF`**：初始化时 `EGR=UG` 之后先清 `SR.UIF`，再开 `DIER.UIE`，否则可能一开中断就先进一次。
- **TIM7 没有通道**：不要在这里写 `CCMR/CCR`；那是 006 的通用定时器内容。
- **优先级不是 005 的主题**：004 已经讲过 NVIC 抢占；005 只有一个 TIM7 中断，用默认/简单优先级即可。
- **F446 的 `VOS` 只在 PLL 打开后生效**：PLL 关闭时稳压器被强制在 scale 3，`PWR_CSR.VOSRDY` 一直是 0。
  先写 `VOS=11` 再等 `VOSRDY` 必然超时；正确顺序是 写 `VOS` → 开 PLL → 等 `PLLRDY` → 等 `VOSRDY`
  （厂商 HAL `stm32f4xx_hal_pwr_ex.c` 的注释写明了这条约束）。005 第一次上板就卡在这里，
  现象是 LD3 常亮、`clock_status = 1`。
- **`TIM7->CNT` 只看得到 0..999**：它反映 1 ms 窗口内的位置；测长周期要看 `ms_ticks` 或 LA。

## 应用层流程图

```text
前置：platform clock 初始化到 180 MHz

初始化：
  RCC->APB1ENR |= TIM7EN；回读
  TIM7->PSC = 89；ARR = 999；CNT = 0
  TIM7->EGR = UG；TIM7->SR &= ~UIF   // 装载并清掉 UG 产生的 UIF
  TIM7->DIER |= UIE
  NVIC_SetPriority(TIM7_IRQn, ...)
  NVIC_EnableIRQ(TIM7_IRQn)
  LD1 熄灭
  TIM7->CR1 |= CEN
  __DSB(); __enable_irq()
        │
        ▼
  while (1) { __WFI(); }             ← 每 1 ms 被 TIM7 唤醒

TIM7_IRQHandler：                     ← 每 1 ms
  if (TIM7->SR & UIF) {
      TIM7->SR &= ~UIF;               // 写 0 清
      ms_ticks++;
      if (++led_divider >= 500) {
          led_divider = 0;
          翻转 LD1;                   // 500 ms 一次 → 1 Hz
      }
  }
```

## 实现

| 文件 | 作用 |
|---|---|
| [`main.c`](main.c) | TIM7 时基 + 1 ms tick + /500 分频翻转 LD1；时钟失败时 LD3 常亮 |
| [`CMakeLists.txt`](CMakeLists.txt) | target `exp005_timer_tick`、烧录入口 `flash_exp005_timer_tick`、`.map/.bin/.hex` 产物 |
| [`board_clock.h`](../../platform/stm32f446ze/clock/board_clock.h) | 目标时钟树常量（唯一来源）+ 频率查询接口 |
| [`board_clock.c`](../../platform/stm32f446ze/clock/board_clock.c) | HSE bypass → PLL 180 MHz 的落地代码 + 编译期约束检查 |

分工按 [README_1](../../README_1.md) 的边界：**时钟是平台事实，tick 是实验事实**。

```c
clock_status = board_clock_init();       // 平台层：VOS / 5 WS / HSE bypass / PLL / APB 分频
...
RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;      // 实验层：TIM7 时基 + 中断
TIM7->PSC = 89; TIM7->ARR = 999;
```

`PSC` / `ARR` 故意写死、不按 `board_clock_apb1_timer_hz()` 反算：这样 LED 周期才是时钟树的“测量仪”，
时钟错了周期按 2×/4× 走样；反算会把时钟树的错悄悄吃掉。

平台把 RM0390 的约束写成了 `_Static_assert`（VCO 输入 1–2 MHz、VCO 100–432 MHz、SYSCLK ≤ 180 MHz、
5 WS、PLLN/PLLQ 范围），改 `board_clock.h` 里任何一个因子都会在编译期重算整棵树。

构建与烧录（E1 入口不变，不新增工程设施）：

```bash
cmake --preset stm32f446ze-debug
ninja -C build/debug flash_exp005_timer_tick   # 构建 + 烧录 + verify + reset
```

固件占用（Debug 快照）：`text 1880 B / data 4 B / bss 1572 B`，RAM 1576 B (1.20%)、ROM 1888 B (0.36%)，
其中 board_clock 约 700 B；Release `-O2` 为 `text 1688 B`。

## 验证与故障注入

**默认验收**

1. LA / 示波器测 PB0：500 ms 高、500 ms 低，周期 1 s。
2. 调试器 watch `ms_ticks`：每秒增加 1000；`TIM7->CNT` 在 0–999 循环；`SR.UIF` 周期性置位、ISR 清掉。
3. 改变 `PSC` / `ARR` 并重新烧录后，周期按公式变化：

   | PSC | ARR | tick | LD1 翻转 | LD1 周期 |
   |---:|---:|---:|---:|---:|
   | 89 | 999 | 1 ms | 500 ms | 1 s |
   | 89 | 1999 | 2 ms | 1 s | 2 s |
   | 179 | 999 | 2 ms | 1 s | 2 s |

4. 周期成 2×/4× 整数倍偏差：先查 APB1 预分频和 APB 定时器 ×2，重新算 `TIM7CLK`；差 1% 左右：HSI/HSE 精度，不是逻辑错。

**故障注入（可选）**

- 在 TIM7 ISR 里加一段 >1 ms 的忙等：`ms_ticks` 会丢 tick，LD1 周期变长。这证明 1 ms 是 CPU 预算，不是硬实时保证；006 会用硬件输出比较避免这种 CPU 抖动。
- 故意不清 `UIF`：ISR 会反复进入，LD1 不再按 1 Hz 走。这是 003 `PR` 之后第二个“必须清标志”的现场。

## 验收记录

2026-11-24，NUCLEO-F446ZE（ST-LINK V2J39M27），Debug 版，SWD 回读 + 计时实测。

**1. 时钟树寄存器回读**（目标运行在 PLL 180 MHz）

| 寄存器 | 期望 | 实测 |
|---|---|---:|
| `RCC->CR` | HSEON + HSEBYP + HSERDY，PLLON + PLLRDY | `0x03077C83` |
| `RCC->PLLCFGR` | M=4 N=180 P=2 Q=7，PLLSRC=HSE | `0x07402D04` |
| `RCC->CFGR` | SW=SWS=PLL，HPRE /1，PPRE1 /4，PPRE2 /2 | `0x0000940A` |
| `FLASH->ACR` | 5 WS + PRFTEN + ICEN + DCEN | `0x00000705` |
| `PWR->CSR` | VOSRDY = 1（scale 1） | `0x00004000` |
| `SystemCoreClock` | 180000000 | `0x0ABA9500` |
| `TIM7->PSC` / `TIM7->ARR` | 89 / 999 | 89 / 999 |
| `TIM7->CR1` / `TIM7->DIER` | CEN=1 / UIE=1 | 1 / 1 |
| `NVIC->ISER[1]` | IRQ55（bit23） | `0x00800000` |

**2. tick 速率**：`ms_ticks` 10 s 内从 476 到 10479（+10003），即 **1000.3 Hz**；
把 `ARR` 改成 1999 重新烧录后 10 s 内 +5001，即 **500.1 Hz**——正好一半，“现象”第 3 条成立。
0.03% 的偏差来自 HSE 精度和主机计时，不是逻辑错。

**3. 波形相位**：以 `ms_ticks` 为基准读 `GPIOB->ODR`：884 ms 亮、1398 ms 灭、1905 ms 亮、2419 ms 灭、
2937 ms 亮，全部满足 `LD1 = (ms_ticks / 500) & 1`，即 500 ms 亮 / 500 ms 灭、周期 1 s。

**4. 失败路径**：Clock checkpoint 第一版在 PLL 关闭时就等 `VOSRDY`，固件给出 LD3 常亮 +
`clock_status = 1`（`BOARD_CLOCK_ERR_REGULATOR`），LED 不再闪——“时钟没起来”是可观察的，不用猜。
修正后的顺序见“容易踩的坑”。

未做：逻辑分析仪直接测 PB0（默认验收第 1 条）与故障注入（第 4 条）留在上板练习时补。

## 硬件连接

**005 不新增硬件**：只用板载 LD1；不需要按键、LD2 或第二块板。

- LD1（绿色） → PB0（SB120 ON、SB119 OFF）
- 高电平 → ON；低电平 → OFF
- 逻辑分析仪（可选）：CH0 → PB0，GND → 板 GND

![[..\001_gpio_output\artifacts\picture\LD1.png]]

005 不使用 B1，也不涉及 C52 / 按键抖动；抖动对照留给 005 之后的 Debounce checkpoint。

## 依据

- RM0390 §6（RCC / 时钟树 / APB prescaler / timer clock）
- RM0390 PWR 章节（VOS 电压调节 / VOSRDY：只有 PLL 打开后 scale 1 才生效）
- RM0390 §19.3.1（TIM6&TIM7 time-base unit）、§19.4（TIM6&TIM7 registers）
- PM0214 §4.3（NVIC，004 已用）
- UM1974 §7.5（LEDs）

[RM0390](../../docs/mcu/STM32F446/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) · [PM0214](../../docs/cortex_m/Cortex-M4/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) · [UM1974](../../docs/board/NUCLEO-F446ZE/user_manual/um1974-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf)
