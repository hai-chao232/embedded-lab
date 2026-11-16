# 实验 001：gpio_output 点灯

> 上级：[experiments/](../README.md)

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

## THINK

这个实验从零开始，点亮了 LD1

分析了：读芯片手册、编译链接、CMake 构建、烧录

作为一个实验，最大的收获不是完成了点灯，而是串起来整个软硬件
