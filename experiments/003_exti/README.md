# 实验 003：exti 外部中断

## 现象

黑盒行为规格：只描述外部可观察的行为，是写代码前的契约、烧录后的验收清单。

1. **初始态**：上电复位后、未操作按键，LD1 保持**熄灭**。
2. **边沿翻转**：每次 B1 的**上升沿**（按下瞬间）翻转 LD1 一次；松开 B1 不改变状态；按住不放也不会持续翻转（理想无抖动时只翻转一次）。这是 002 刻意留给 003 的「按一次翻转」。
3. **事件驱动**：主循环不再查询按键，CPU 在 `__WFI()` 中睡眠；按键事件唤醒 CPU，由 ISR 完成翻转。按下到翻转的延迟**人眼不可察觉**。
4. **边界现象（可能出现、必须能解释）**：一次物理按压可能因触点抖动产生多个上升沿，LD1 可能翻转多次，也可能因 pending 合并只翻转一次；松手后的最终状态取决于抖动边沿与 ISR 的时序，可能亮也可能灭。这是 002 物理层的延续，不是 EXTI 配置错误；本实验不做消抖。

第 1–3 条必须成立；第 4 条允许出现，且出现时要能解释。

## 目的

核心一句话：**把 002 的「CPU 反复问」换成「硬件在事件发生时叫醒 CPU」**。

002 的代价：主循环忙等，CPU 的时间花在反复读 IDR 上，两次查询之间的事件会丢。003 让 PC13 的边沿在 EXTI 里变成 pending 锁存，再由 NVIC 打断 CPU；CPU 不需要知道按键什么时候来，只在 ISR 里处理。

数据流对照：

```text
002（轮询）：CPU 循环读 IDR → 发现电平 → 决策 → 写 BSRR
003（中断）：PC13 边沿 → EXTI 置 pending → NVIC 中断 → ISR 清 pending/翻转 → 返回
```

| | 002 轮询 | 003 中断 |
|---|---|---|
| 谁发现事件 | CPU 读 IDR | EXTI 硬件 |
| 事件锁存 | 无（采样窗口） | PR pending |
| 主循环 | 忙等 | `__WFI()` 睡眠 |
| 抖动 | 多次采样 | 多次中断/合并 |
| 响应 | 取决于循环周期 | 取决于 NVIC/ISR 延迟 |

整条链路一句话：`PC13 → SYSCFG_EXTICR4 → EXTI13（RTSR/IMR/PR）→ NVIC IRQ40 → EXTI15_10_IRQHandler → PB0`。

三层认知增量，每层挂一个可观察的验证：

- **外设/寄存器层**：把「引脚 → 边沿 → pending → NVIC → ISR」这条硬件链路走通。
  - `SYSCFG_EXTICR4` 把 EXTI13 路由到 PORTC；
  - `EXTI_RTSR` 选上升沿，`EXTI_FTSR` 关下降沿；
  - `EXTI_IMR` 解除屏蔽，`EXTI_PR` 锁存 pending；
  - NVIC 使能 IRQ 40（`EXTI15_10_IRQn`）；
  - ISR 里检查 `PR13`，并**写 1 清除**（写 0 无效）。
  验证：断点打在 `EXTI15_10_IRQHandler`，按 B1 能命中；`EXTI->PR` bit13 按下置 1、ISR 清 0。
- **架构层**：事件驱动 vs 轮询；主循环 `while (1) { __WFI(); }`，CPU 睡眠；ISR 是「事件回调」，只做最小动作；响应延迟由 NVIC/ISR 决定，不再由循环周期决定。
  - 关键区别：IDR 是**采样窗口**，PR 是**单 bit 锁存器**；CPU 在睡眠或忙别的事时，事件不会因为「没轮到读」而完全消失——但 PR 不是队列，同一 line 的多个边沿在 ISR 处理前会合并成一次 pending。
  - 共享向量：`EXTI15_10` 共用一个 IRQ，ISR 必须先判断是不是 line13，不能假设「进了 ISR 就一定是 B1」。
  - 验证：按住 B1 不放，稳定后只翻转一次（抖动除外）；松开不翻转；用逻辑分析仪测 PC13 上升沿到 PB0 翻转的延迟。
- **物理层**：003 不新增硬件，仍是 B1（PC13）+ LD1（PB0）；C52 无件，抖动会变成多个上升沿（中断可能合并）。EXTI 解决的是「轮询可能漏事件」，不是「触点抖动」；消抖不在本实验范围。
  - 验证：快速点按，观察是否一次按压翻转多次；用逻辑分析仪抓 PC13 的抖动边沿，数上升沿个数，再和 LD1 的实际翻转次数对照。

刻意不做：不消抖（抖动留作现象 4）、不做优先级/抢占（[004](../004_nvic/README.md)）、不在 ISR 里做延时/打印/复杂处理。单实验单认知点。

消抖对照不在本实验做：计划在 005（timer 基本定时中断）之后，作为 “Debounce checkpoint” 回访 003，对比裸 EXTI 与定时采样消抖；详见 [ROADMAP](../../ROADMAP.md)。

工程线：003 继续复用 E1 的构建/烧录入口，不需要新的工程设施；新增的只有 SYSCFG / EXTI / NVIC 初始化。

## 关键寄存器与操作

| 寄存器/位或操作 | 作用 | 本实验设置 |
|---|---|---|
| `RCC->APB2ENR.SYSCFGEN` | SYSCFG 时钟 | 1 |
| `SYSCFG->EXTICR[3].EXTI13` | PC13 → EXTI13 | `0b0010`（PORTC） |
| `EXTI->RTSR.TR13` | 上升沿触发 | 1 |
| `EXTI->FTSR.TR13` | 下降沿触发 | 0 |
| `EXTI->IMR.IM13` | 中断屏蔽 | 1（不屏蔽；0 才是屏蔽） |
| `EXTI->PR.PR13` | 挂起标志 | 写 1 清除；写 0 无效；单 bit 锁存 |
| `NVIC_EnableIRQ(EXTI15_10_IRQn)` | 使能 IRQ40 | 调用即可（ISER1 bit8） |
| `__enable_irq()` | 清 PRIMASK | 开全局中断 |
| `__WFI()` | 睡眠等待中断 | 主循环 |

三个容易踩的坑：

- `EXTI->IMR` 的 0/1 语义容易记反：**0 = 屏蔽，1 = 不屏蔽**。
- `EXTI->PR` 是**写 1 清除**；不要写 `EXTI->PR &= ~EXTI_PR_PR13`，那样 pending 不会被清掉，ISR 会反复进。
- `EXTI15_10_IRQHandler` 是 line10–15 共用的；先判断 `PR13`，再处理。

补充：EXTI 没有独立的 RCC 使能位，要开的是 SYSCFG 时钟；`SYSCFG_EXTICR4` 和 `EXTI->IMR/RTSR/FTSR` 都按位改，不要整寄存器覆盖，避免清掉其他 line 的配置。

## 应用层流程图

```text
初始化：
  RCC 使能 GPIOB / GPIOC / SYSCFG
  PB0 推挽输出；PC13 输入
  SYSCFG->EXTICR[3] = PORTC（EXTI13）
  EXTI->RTSR |= TR13；FTSR &= ~TR13
  EXTI->PR = PR13（清 pending）
  EXTI->IMR |= IM13
  NVIC_ClearPendingIRQ(EXTI15_10_IRQn)
  NVIC_EnableIRQ(EXTI15_10_IRQn)
  __enable_irq()
  LD1 熄灭
        │
        ▼
  while (1) { __WFI(); }   ← CPU 睡眠，等待事件
        ▲
        │  B1 上升沿（硬件自动）
        │
  EXTI13 pending → NVIC IRQ40 → EXTI15_10_IRQHandler
        │
        ▼
  if (EXTI->PR & PR13) {
      EXTI->PR = PR13;          // 写 1 清 pending
      读 ODR 判断 PB0 当前电平；
      用 BSRR 原子翻转 PB0；   // LD1 亮/灭
  }
```

EXTICR 把 16 条 EXTI 线映射到 GPIO 端口（RM0390 Figure 31）：

![[artifacts\picture\EXTI_GPIO_MAP.png]]

## 硬件连接

**003 不新增硬件**：B1 和 LD1 都是 NUCLEO-F446ZE 板载，接线与 002 完全相同；没有外部按键、LED 或第二块板。

- B1 USER(Blue) → PC13（SB173 ON、SB180 OFF）
- LD1（绿色） → PB0（SB120 ON、SB119 OFF）
- R58 220kΩ 外部下拉；C52 无件（无硬件消抖）

```text
VDD ── B1 触点 ── 节点A
节点A ── R59 330Ω ── SB173 ── PC13
节点A ── R58 220kΩ ── GND
节点A ── C52[N/A] ─ R60 100Ω ── VDD
```

板级细节与 002 相同，见 [002 的硬件连接](../002_gpio_input/README.md#L74-L84)。

![[..\002_gpio_input\artifacts\picture\B1_button.png]]

![[..\001_gpio_output\artifacts\picture\LD1.png]]

调试连接：003 主循环是 `__WFI()`，OpenOCD 需要 `connect_assert_srst` 才能可靠连上睡眠中的目标；平台已配置，见 [platform README](../../platform/stm32f446ze/README.md#L19-L23)。

## 依据

- RM0390 §10.2（EXTI）、§10.1（NVIC）、§8.2.6（SYSCFG_EXTICR4）
- PM0214 §4.3（NVIC，Cortex-M4 侧）
- UM1974 §7.6（Push-buttons，B1→PC13）
- MB1137 原理图第 2 页（B1 / R58 / R59 / C52）

[RM0390](../../docs/mcu/STM32F446/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) · [PM0214](../../docs/cortex_m/Cortex-M4/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) · [UM1974](../../docs/board/NUCLEO-F446ZE/user_manual/um1974-stm32-nucleo144-boards-mb1137-stmicroelectronics.pdf) · [MB1137](../../docs/board/NUCLEO-F446ZE/schematic/MB1137.pdf)
