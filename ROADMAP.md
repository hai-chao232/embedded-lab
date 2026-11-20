# embedded-lab 学习路线与实验清单

状态图例：⬜ 未开始 · 🟡 进行中 · ✅ 完成 · — 不适用

本路线同时推进两条能力线：

- **硬件机制线**：通过实验逐步理解 MCU、外设、总线、中断、DMA、通信协议和 RTOS。
- **工程能力线**：逐步完善构建、调试、产物分析、可复现环境、CI、静态分析和发布流程。

原则是：**工程能力在“有真实问题可解释时”引入，不为了基础设施本身暂停实验主线。**

---

## Level 约定

| Level | 手段 | 说明 |
|:-----:|------|------|
| L1 | 寄存器 / CMSIS | 直接操作寄存器，理解硬件 |
| L2 | STM32 LL | 厂商底层抽象 |
| L3 | HAL | 快速工程开发 |

- 同一外设尽量按 L1 → L2 → L3 递进；一个实验可以混合层次，在 README 里写清哪部分用哪个。
- 例外：USB 直接从框架级开始（TinyUSB / ST USB Device），不手搓 Chapter 9。

---

## P0：仓库与裸机构建基线 ✅

P0 的目标不是“做完整工程系统”，而是建立一个**最小、可理解、可继续扩展的裸机仓库基线**。

已完成：

- [x] 仓库目录骨架确定，不再做架构级调整。
- [x] CMake + Ninja 构建链路跑通。
- [x] GNU Arm Embedded Toolchain 可用于 `arm-none-eabi` 交叉编译。
- [x] STM32CubeF4 v1.28.3 通过 Git submodule 接入并锁定版本。
- [x] STM32F446ZE CMSIS / startup / `system_stm32f4xx.c` 接入。
- [x] STM32F446ZE linker script 接入。
- [x] Cortex-M4F 基础编译 / 链接参数生效。
- [x] `001_gpio_output` 最小程序可生成 ELF。
- [x] 已使用 `nm` / `size` 等工具初步分析最小 ELF。
- [x] 已建立第三方许可。

P0 **有意不包含**以下工程增强，它们被明确安排到 E1，而不是遗忘：

- `-Wall / -Wextra / -Wpedantic`
- linker `.map`
- post-build `.bin / .hex`
- 自动 `size`
- Debug / Release 构建模型

完成本节后进入实验 001，不再继续扩建仓库骨架。

---

## 第一阶段：MCU 底层机制（14 次）

> 顺序设计：
>
> - 001 首次完成“真实 GPIO → 烧录 → SWD/GDB → 仪器/寄存器验证”，随后执行 E1。
> - 004 完成后、005 Timer 开始前，完成平台时钟 checkpoint。
> - DMA 先作为独立能力理解（008），再应用到 ADC / UART（010 / 014）。
> - UART 走“轮询 → 中断 → ring buffer → DMA”四步。
> - 007 输入捕获不用信号发生器：第二块 F446ZE 输出 PWM，第一块捕获，LA1010 同时测两块。

| 编号 | 实验 | Level | 依赖 | 硬件 | 状态 |
|:----:|------|:-----:|------|------|:----:|
| 001 | gpio_output 点灯 | L1（对比 L2/L3） | P0 | 板载 LED + 调试器（万用表 / LA 可选） | ✅ |
| 002 | gpio_input 按键轮询 | L1 | 001 + E1 | 按键 | ⬜ |
| 003 | exti 外部中断 | L1 | 002 | 按键 + LED | ⬜ |
| 004 | nvic 优先级与抢占 | L1 | 003 | 按键 + LED | ⬜ |
| 005 | timer 基本定时中断 | L1 | 004 + Clock checkpoint | LED | ⬜ |
| 006 | timer PWM 呼吸灯 | L2 | 005 | LED + LA | ⬜ |
| 007 | timer 输入捕获测频 | L2 | 006 | 板2 PWM 输出 + LA | ⬜ |
| 008 | dma mem2mem 搬运 | L1 | 005 | 调试器 | ⬜ |
| 009 | adc 电位器采样 | L2 | 005 | 电位器 | ⬜ |
| 010 | adc + timer 触发 + 环形 DMA | L2/L3 | 008, 009 | 电位器 + LA | ⬜ |
| 011 | uart 轮询收发 | L1 | 004 | CH340 | ⬜ |
| 012 | uart 中断收发 | L1 | 011 | CH340 | ⬜ |
| 013 | uart ring buffer | L1/L2 | 012 | CH340 | ⬜ |
| 014 | uart DMA 收发 | L2 | 008, 013 | CH340 | ⬜ |

### 001 的阶段目标

001 不只是“点亮 LED”。它负责第一次完整跑通：

1. 根据 Datasheet / Reference Manual / 开发板手册确认 GPIO 与板载 LED。
2. 理解 RCC 和 GPIO 的必要寄存器。
3. 使用 CMSIS 寄存器方式完成 GPIO 输出。
4. 编译、链接并分析固件。
5. 使用 OpenOCD / ST-LINK 烧录。
6. 使用 SWD/GDB 查看寄存器、单步和断点。

---

## E1：工程基线 v1（001 完成后、002 开始前）✅

E1 是 P0 的工程化补全点：001 已经能生成 ELF，E1 把“构建 → 烧录 → 产物分析 → 调试/发布”补成可重复的基线。每项都围绕 001 的真实产物做，不提前造基础设施。

进度：E1.0 ✅ · E1.1 ✅ · E1.2 ✅ · E1.3 ✅

| 项 | 解决的问题 | 实现落点 | 关键验收 |
|---|---|---|---|
| **E1.0 烧录入口** | 每个实验都要手写 OpenOCD、知道调试器品牌 | 板级 `openocd.cfg` + `scripts/flash.sh` + `stm32f446ze_add_flash_target()` | 一条 `ninja -C build/debug flash_exp001_gpio_output` 完成构建 + 烧录 + verify + reset |
| **E1.1 告警基线** | 告警不可见、质量随实验漂移 | `cmake/warnings.cmake`，由 platform 自动继承 | `-Wall -Wextra -Wpedantic` 生效；编译 0 告警 |
| **E1.2 产物与链接分析** | 只有 ELF，Flash 占用和链接来源不可见 | `cmake/firmware.cmake` + `embedded_add_firmware_artifacts()` | `.map/.bin/.hex/size` 齐全；`.bin` 与 verify 字节对照 |
| **E1.3 Debug/Release** | 无调试信息，Debug/Release 混用 | `cmake/build_type.cmake` + 两个 preset | Debug `-Og -g3` / Release `-O2 -DNDEBUG`；`build/debug`、`build/release` 分离 |

**为什么先做 E1.0**：没有稳定的烧录入口，后面的告警、产物、构建类型都没有“真实上板”的验证对象；OpenOCD 版本、ST-Link 驱动、bash/CRLF 这些主机侧问题，也越早收进基线越不会污染后续实验。

四条的共同模式：

- **真实问题驱动**：每条都对应 001 已经出现的一个具体不便。
- **项目层单点**：策略只写一份，实验只表达意图（链接 platform，烧录/产物各一行）。
- **继承优先**：能自动继承就自动（warnings、build type）；不能继承就显式一行（artifacts）。
- **证据闭环**：数字、命令、踩坑都记录；详细记录见 [E1 工程基线笔记](docs/notes/E1-engineering-baseline.md)。

关键结果（2026-11-19）：

- 编译 0 告警；链接 4 条 nosys 虚警，`.map` 确认被 `--gc-sections` 回收。
- Debug：elf 689104 / bin 872 / size 872 / 0 / 1568；Release：elf 11424 / bin 872。
- Debug `gdb info line main` = `main.c:6 @ 0x8000250`；Release 无行号信息。
- `-g3` 选 `libg_nano.a`，Release 走 `libc_nano.a`；调试信息不占 Flash。

2026-11-20 补验：

- 上板源码级调试：`break main.c:6`、next、`print/x RCC->AHB1ENR`=0x2、`GPIOB->ODR`=0x1；001 第 6 条闭环。
- GUI：`.vscode/launch.json` + `tasks.json`，Cortex-Debug 复用 `openocd.cfg`，preLaunchTask 走 `cmake --build --preset debug`。设计上 F5 = 构建 + 下载 + 停在 main；界面侧待首次运行验证。首次/结构变化后仍需先 `cmake --preset stm32f446ze-debug`。
- 待补：STM32F446.svd；OpenOCD/GDB/objdump 路径目前写死，E2 统一。

留待 E2 / 未验证：

- Release 版实际烧录运行；`.bin/.hex` 实际烧录（当前 flash 入口走 elf）。
- `common` 的告警继承（空壳库，等有真实源文件回补）。
- `-Og` 与 `-O2` 在当前小实验恰好同尺寸，不代表一般规律。

---

## Clock checkpoint：004 → 005

在 Timer 实验开始之前完成平台时钟基线：

- 理解 HSI / HSE / PLL / AHB / APB 时钟树。
- 明确 NUCLEO-F446ZE 当前外部时钟来源与板级连接。
- 配置目标系统时钟：HSE bypass → PLL → 180 MHz（如最终平台方案保持该目标）。
- 正确配置 Flash latency、总线分频和相关电源 / 时钟约束。
- 更新 `SystemCoreClock`。
- 用可观测手段验证实际频率，而不是只相信寄存器配置。
- 特别理解 APB prescaler 与 Timer clock 倍频规则。

Timer / PWM / Capture 之后都以此时钟基线为前提。

---

## E2：第一阶段工程收尾（014 完成后）

E2 的目标是从“在当前开发机上能稳定构建”升级到“仓库能够持续、可重复地验证”。

计划内容：

- 精确化 GNU Arm Toolchain 版本策略，研究可复现安装 / pinning。
- 新机器从零构建的 bootstrap 流程。
- CI 至少执行 configure + build，覆盖第一阶段实验。
- 评估在 CI 对自有代码启用 `-Werror`。
- 逐步评估更严格 warning，例如 `-Wshadow`、`-Wundef`、`-Wconversion` 等，而不是机械全开。
- 引入合适的静态分析（如 clang-tidy / cppcheck，具体方案届时根据代码结构决定）。
- 检查不同 Debug / Release 配置的构建稳定性。
- 形成第一版“环境、构建、调试、产物分析”可复现文档。

E2 完成后，第一阶段不仅外设知识闭环，工程基线也完成一次升级。

---

## 第二阶段：通信能力（草案，编号可能调整）

| 编号 | 实验 | Level | 硬件 | 状态 |
|:----:|------|:-----:|------|:----:|
| 015 | i2c bit-bang 软件模拟 | L1 | 24LC256 + LA | ⬜ |
| 016 | i2c 硬件外设 | L2 | 24LC256 + LA | ⬜ |
| 017 | spi 寄存器级 | L1 | W25Q64 + LA | ⬜ |
| 018 | spi 硬件外设 + JEDEC ID | L2 | W25Q64 + LA | ⬜ |
| 019 | qspi 四线模式提速对比 | L3 | W25Q64 + LA | ⬜ |
| 020 | eeprom / flash 读写与故障注入 | L2/L3 | 24LC256 / W25Q64 | ⬜ |
| 021 | can loopback + 双板对发 | L2 | CAN 收发器（待购） | ⬜ |
| 022 | usb cdc 虚拟串口 | 框架级 | USB 线 | ⬜ |

第二阶段同时继续完善：

- 自动化烧录 / 调试命令。
- 协议波形与关键测试证据归档。
- 通信错误与恢复路径的故障注入。
- 必要的 host-side 辅助脚本。

---

## 第三阶段：软件架构（对应 `frameworks/`，规划中）

Super Loop（天然贯穿早期实验）→ Protothreads → EventOS Nano → QP-nano
→ FreeRTOS → ChibiOS → Zephyr。

边界与集成策略见 `frameworks/README.md`。

这一阶段的工程能力重点逐步转向：

- 模块边界与接口。
- host-side test。
- 静态分析。
- RTOS 调试与运行时观测。
- 不同 framework 的构建隔离与可重复集成。

---

## 第四阶段：网络（规划中）

TCP/IP + MQTT，与 Linux 主机通信。

STM32F446 本身无以太网 MAC，需要补网络模块（例如 W5500 SPI 以太网或 Wi-Fi 模块），硬件届时确定。

重点加入：

- 网络抓包。
- 超时 / 断线 / 重连故障注入。
- 长时间运行稳定性测试。
- 主机端测试工具与日志分析。

---

## 第五阶段：综合项目（`projects/`，规划中）

用前四阶段能力完成一个接近真实产品的完整嵌入式设备。

最终工程能力目标包括：

- Debug / Release 正式发布流程。
- 固件版本信息。
- 构建产物归档。
- Flash / RAM size budget。
- 自动化构建与基本测试。
- 第三方依赖与许可证复核。
- 可复现的发布说明。
