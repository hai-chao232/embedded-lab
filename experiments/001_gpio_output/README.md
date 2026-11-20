# 实验 001：gpio_output 点灯

## 目的

建立 “寄存器控制 GPIO” 的完整认知：

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

## 应用层流程图
![[artifacts\picture\flow_diagram.png]]


## 硬件连接

直接使用 NUCLEO-F446ZE 板载用户 LD1 。

![[artifacts\picture\LD1.png]]

板载 LED 为高电平点亮

