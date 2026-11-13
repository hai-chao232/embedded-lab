# 实验 001：gpio_output 点灯

> 上级：[experiments/](../README.md)
>
> 封口状态：**✅ L1 核心闭环完成（2026-09-29）**

## 目的

建立“寄存器控制 GPIO”的第一层完整认知：

```text
RCC 门控时钟
  ↓
GPIO 寄存器配置
  ↓
输出数据寄存器
  ↓
PB0 物理电平
  ↓
板载 LD1
```

## 硬件连接

直接使用 NUCLEO-F446ZE 板载用户 LED。

板载 LED 为高电平点亮（来源：UM1974 §7.5，官方资料归档见 `docs/board/NUCLEO-F446ZE/user_manual/`）。

| MCU 引脚 | 方向 | 板载对象 | 有效电平 | 备注 |
|----------|------|----------|----------|------|
| PB0 | OUT | LD1（绿） | HIGH | 默认焊桥 SB120 ON / SB119 OFF；可切至 PA5（Arduino D13） |
| PB7 | OUT | LD2（蓝） | HIGH | 本实验不用 |
| PB14 | OUT | LD3（红） | HIGH | 本实验不用 |

001 只操作 **GPIOB / PB0 / LD1**。

## 最终寄存器配置

| 目标 | 寄存器 / field | 目标值 | 含义 |
|------|----------------|--------|------|
| 打开 GPIOB 时钟 | `RCC_AHB1ENR.GPIOBEN` | `1` | GPIOB 获得 AHB1 外设时钟 |
| PB0 模式 | `GPIOB_MODER[1:0]` | `01` | General-purpose output |
| PB0 输出类型 | `GPIOB_OTYPER[0]` | `0` | Push-pull |
| PB0 输出速度 | `GPIOB_OSPEEDR[1:0]` | `00` | Low speed |
| PB0 上下拉 | `GPIOB_PUPDR[1:0]` | `00` | No pull-up / no pull-down |
| PB0 输出值 | `GPIOB_ODR[0]` | `1` | HIGH → LD1 ON |
