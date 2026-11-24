# 实验 004：nvic 优先级与抢占

## 现象

黑盒行为规格：只描述外部可观察的行为，是写代码前的契约、烧录后的验收清单。

实验有 A / B 两种优先级配置，**默认 A**；B 需要交换代码里的两个优先级宏后重新烧录。

1. **初始态**：上电复位后 LD1 熄灭；LD2 以约 5 Hz 持续闪烁（SysTick 心跳，每 100 ms 翻转一次）。
2. **按一次 B1**：EXTI13 上升沿进入 `EXTI15_10_IRQHandler`，LD1 点亮约 0.5 s 量级后自动熄灭。这不是“按住亮 / 松开灭”——LD1 的亮灭由 ISR 的忙等窗口决定，松手不会提前结束。
3. **A（默认，可抢占）**：LD1 亮的窗口内，LD2 **继续闪烁**；SysTick（优先级 0）打断了 EXTI（优先级 1）。调试器 watch `systick_ticks`，窗口内计数继续增加。
4. **B（交换优先级，不可抢占）**：LD1 亮的窗口内，LD2 **停止闪烁**；SysTick 的 pending 被压住，直到 EXTI ISR 返回后才补一次，然后恢复闪烁。`systick_ticks` 在窗口内不增加。
5. **边界现象**：B1 的触点抖动可能触发多个 EXTI 窗口，LD1 连续亮多个约 0.5 s 的窗口；这是 003 物理层的遗留，不是 NVIC 配置错误。004 不消抖。

第 1–3 条（默认 A）必须成立；第 4 条是 B 配置的对照；第 5 条允许出现，且出现时要能解释。

## 目的

核心一句话：**003 证明“事件能打断主循环”，004 证明“中断之间也能互相打断，但只按优先级”。**

003 只有一条中断；004 同时打开 EXTI13（按键）和 SysTick（100 ms 心跳），让两个 ISR 竞争 CPU。交换优先级，同一个 EXTI 忙等窗口里的 SysTick 行为就变了：

```text
A（默认，SysTick 0 > EXTI 1）——可抢占
  EXTI    |===== LD1 on =====================|
  SysTick      |t|   |t|   |t|   |t|   |t|      ← 插入，LD2 继续闪

B（交换，EXTI 0 > SysTick 1）——不可抢占
  EXTI    |===== LD1 on =====================|
  SysTick  (pending，不能插入)             |t|  ← 返回后才执行
```

| 配置 | EXTI 抢占优先级 | SysTick 抢占优先级 | 结果 |
|---|---:|---:|---|
| A（默认） | 1 | 0 | SysTick 抢占 EXTI |
| B | 0 | 1 | SysTick 等 EXTI 返回 |

两个 ISR 的职责刻意做得很小/很直白：

- **EXTI ISR（A 组低优先级 / B 组高优先级）**：清 `PR13`，点亮 LD1，忙等约 0.5 s 量级制造一个可观察的“正在执行”窗口，然后熄灭 LD1。
- **SysTick ISR（A 组高优先级 / B 组低优先级）**：每 100 ms 递增 `systick_ticks`，翻转 LD2。

三层认知增量，每层挂一个可观察的验证：

- **寄存器层**：把 NVIC 的优先级、使能和 SysTick 的时基配置讲清楚。
  - `SCB->AIRCR.PRIGROUP = 0`：4 位抢占优先级，0 位子优先级；
  - `NVIC->IP[40]`：EXTI15_10 的优先级；`SCB->SHP[11]`：SysTick 的优先级；
  - 数字越小，优先级越高；
  - `NVIC_SetPriority()` 负责把优先级编码进 IP/SHP；
  - `SysTick->LOAD/VAL/CTRL`：100 ms 心跳。
  - 验证：调试器读 `NVIC->IP[40]`、`SCB->SHP[11]`、`SysTick->CTRL`；watch `systick_ticks`。
- **架构层**：抢占（preemption）与子优先级（subpriority）的区别。
  - 高抢占优先级可以打断正在执行的低抢占优先级 ISR；低优先级只能等；
  - 同一抢占优先级之间**不会**互相抢占，子优先级只决定“同时 pending 时谁先走”；
  - 被压住的 SysTick 只有一个 pending 位，不会排队；EXTI 返回后补执行一次；
  - 长 ISR 会拉长低优先级中断的响应时间——这是本实验故意放大的现象，不是推荐写法。
  - 验证：A/B 对照；用逻辑分析仪同时看 PB0（LD1）和 PB7（LD2），A 组 PB7 的边沿落在 PB0 高电平内部，B 组落在 PB0 下降沿之后。
- **物理层**：004 不新增硬件，仍是 B1（PC13）+ LD1（PB0）+ LD2（PB7）；C52 无件，抖动会触发多个 EXTI 窗口。SysTick 是 Cortex-M4 内核定时器，不是 005 要讲的通用 TIM；005 再展开 TIM、时钟树和 prescaler。
  - 验证：按一次 B1 看 LD1/LD2 的配合；快速点按看是否出现多个窗口；用 LA 测窗口宽度和 SysTick 边沿位置。

刻意不做：不消抖（留给 005 之后的 Debounce checkpoint）、不做临界区 `PRIMASK/BASEPRI`（后续需要时再讲）、不做动态优先级修改、不做子优先级的代码实验（004 固定 `PRIGROUP=0`，把“抢占”讲透）。单实验单认知点。

工程线：004 继续复用 E1 的构建/烧录入口，只新增 NVIC / SysTick 配置，不需要新的工程设施。

## 关键寄存器与操作

| 寄存器/位或操作 | 作用 | 本实验设置 |
|---|---|---|
| `SCB->AIRCR.PRIGROUP` | 优先级分组 | 0（4 位抢占，0 位子优先级） |
| `NVIC->IP[40]` / `NVIC_SetPriority(EXTI15_10_IRQn, ...)` | EXTI15_10 优先级 | A：1；B：0 |
| `SCB->SHP[11]` / `NVIC_SetPriority(SysTick_IRQn, ...)` | SysTick 优先级 | A：0；B：1 |
| `NVIC->ISER[1]` bit8 / `NVIC_EnableIRQ` | EXTI IRQ 40 使能 | 1 |
| `SysTick->LOAD` | 100 ms 重装值 | `SystemCoreClock / 10 - 1` |
| `SysTick->VAL` | 当前计数值 | 0（写清零） |
| `SysTick->CTRL` | CLKSOURCE / TICKINT / ENABLE | 1 / 1 / 1 |
| `EXTI->PR` | 清 EXTI13 pending | 写 1 清除 |
| `PRIMASK` / `BASEPRI` | 全局 / 临界区屏蔽 | 本实验不用，后续再讲 |

几个容易踩的坑：

- **数字越小，优先级越高**；A 组 SysTick=0、EXTI=1，所以 SysTick 能打断 EXTI。
- **`NVIC_SetPriorityGrouping(0)` 要在设优先级之前**；否则 `NVIC_SetPriority` 的编码口径可能不是你想要的。
- **`NVIC->IP[40]` 只有高 4 位实现**（STM32F446 的 `__NVIC_PRIO_BITS=4`），写 0/1 时由 `NVIC_SetPriority` 处理移位，不要手写 `NVIC->IP[40] = 1`。调试器里 priority 1 会显示成 `0x10`，priority 0 显示 `0x00`，不是写错。
- **`SysTick->LOAD` 是 24 位**：16 MHz 下 100 ms = 1,600,000，放得下；005 时钟基线如果升到 180 MHz，100 ms = 18,000,000，放不下，需要改 tick 或分频。
- **SysTick 不是 TIM**：这里只用它当“第二个异常源 + 100 ms 心跳”，005 再讲通用定时器。

## 应用层流程图

```text
初始化：
  RCC 使能 GPIOB / GPIOC / SYSCFG
  PB0 / PB7 推挽输出；PC13 输入
  EXTI13 上升沿；NVIC 使能 IRQ40
  NVIC_SetPriorityGrouping(0)
  NVIC_SetPriority(EXTI15_10_IRQn, EXTI_PRIORITY)
  NVIC_SetPriority(SysTick_IRQn, SYSTICK_PRIORITY)
  SysTick 100 ms：LOAD / VAL / CTRL
  __DSB(); __enable_irq()
  LD1 / LD2 熄灭
        │
        ▼
  while (1) { __WFI(); }         ← 主循环睡眠，SysTick 每 100 ms 唤醒

EXTI15_10_IRQHandler：           ← B1 上升沿
  if (EXTI->PR & PR13) {
      EXTI->PR = PR13;           // 写 1 清 pending
      LD1 亮;
      busy wait ≈ 0.5 s;         // 抢占窗口（故意长，非推荐写法）
      LD1 灭;
  }

SysTick_Handler：                ← 每 100 ms
  systick_ticks++;
  翻转 LD2;
```

## 硬件连接

**004 不新增硬件**：B1、LD1、LD2 都是 NUCLEO-F446ZE 板载，接线与 003 完全相同；没有外部按键、LED 或第二块板。

- B1 USER(Blue) → PC13（SB173 ON、SB180 OFF）
- LD1（绿色） → PB0（SB120 ON、SB119 OFF）
- LD2（蓝色） → PB7
- LD3（红色） → PB14（本实验不用，可留作第三标记）
- R58 220kΩ 外部下拉；C52 无件（无硬件消抖）

逻辑分析仪（可选）：CH0 → PB0（LD1），CH1 → PB7（LD2），GND → 板 GND；不需要额外器件。

调试连接：004 主循环是 `__WFI()`，OpenOCD 需要 `connect_assert_srst` 才能可靠连上睡眠中的目标；平台已配置，见 [platform README](../../platform/stm32f446ze/README.md#L19-L23)。

```text
VDD ── B1 触点 ── 节点A
节点A ── R59 330Ω ── SB173 ── PC13
节点A ── R58 220kΩ ── GND
节点A ── C52[N/A] ─ R60 100Ω ── VDD
```

板级细节与 002 相同，见 [002 的硬件连接](../002_gpio_input/README.md#L74-L84)。

![[..\002_gpio_input\artifacts\picture\B1_button.png]]

![[..\001_gpio_output\artifacts\picture\LD1.png]]

## 依据

- RM0390 §10.1（NVIC）、§10.2（EXTI）
- PM0214 §4.3（NVIC）、§4.4.5（AIRCR / PRIGROUP）、§4.5（SysTick）
- UM1974 §7.5（LEDs：LD1/LD2/LD3）、§7.6（Push-buttons，B1→PC13）
- MB1137 原理图第 2 页（B1 / R58 / R59 / C52）

[RM0390](../../docs/mcu/STM32F446/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) · [PM0214](../../docs/cortex_m/Cortex-M4/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) · [UM1974](../../docs/board/NUCLEO-F446ZE/user_manual/um1974-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf) · [MB1137](../../docs/board/NUCLEO-F446ZE/schematic/MB1137.pdf)
