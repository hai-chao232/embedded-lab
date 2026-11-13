# experiments/ 实验目录

> 上级：[根 README](../README.md)

## 结构

每个实验一个目录，命名 `0XX_<topic>`（编号与清单见根目录 `ROADMAP.md`）。

```
STM32CubeF4 ──▶ platform/common ──▶ Experiment001 / 002 / 003 …
```

公共 MCU 基础设施只有一份（third_party + platform + common）。
**每个实验只保留真正属于自己的文件**
