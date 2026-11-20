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

E1 是 P0 的工程化补全点。此时已经有真实可运行 firmware，每项增强都必须结合 001 的实际产物理解。

进度：E1.0 ✅ · E1.1 ✅ · E1.2 ✅ · E1.3 ✅

### E1.0 烧录入口

做什么：

- `platform/stm32f446ze/openocd.cfg` （板级 `source` 组合，实验不该知道调试器品牌）
- CMake `flash_<target>` ，用 `$<TARGET_FILE:...>` 传产物，不写死路径

验收：`ninja -C build flash_exp001_gpio_output` 能从干净状态一步烧录并复位

✅ **已完成并端到端验证** — 环境：Windows 11 / xPack OpenOCD 0.12.0-7 / MSYS2 bash / ARM GNU Toolchain 15.3.1

- `ninja -C build flash_exp001_gpio_output` 一步走完构建 + 烧录 + verify + reset。
- `verified 916 bytes`；elf 为 11468 字节，差额是 section header / 符号表等不进 flash 的部分（留给 E1.2 用 `.map` 拆开）。
- ST-Link 驱动用 Keil5 自带的 STSW-LINK009 v2.0.2：`ProblemCode 28 → 0`，Service = WinUSB。
- 烧录逻辑收在 `scripts/flash.sh` 单点，CMake 只传参；实验侧只见 `stm32f446ze_add_flash_target()`，看不到调试器品牌与固件路径。
- **坑 1**：OpenOCD 0.12 的 `interface/stlink.cfg` 已改用 `adapter driver st-link`（dapdirect 模型），只接受 `jtag/swd/swim`。旧写法 `transport select hla_swd` 报 `Debug adapter doesn't support 'hla_swd' transport`；`interface/stlink-dap.cfg` 与 `transport select dapdirect_swd` 在本版本也均已 deprecated。
- **坑 2**：直接删掉 `transport select` 那行也能烧通，但 openocd 会兜底并每次打印 `DEPRECATED: auto-selecting transport "swd (dapdirect)"`。必须显式写 `transport select swd`。根因：`target/swj-dp.tcl` 里 `[catch {transport select}]` 要求 transport 必须已有值。
- **坑 3**：`verify_image` 在 target 未 halt 时会报 `not halted (start target algo)` 并回退路径，结果不可信；先 `halt` 再 verify 才是干净比对。
- **坑 4**：`OPENOCD` 环境变量要写正斜杠（`D:/app/...`）。该值会传给 MSYS2 bash，反斜杠被当转义符吃掉，`command -v` 就失败。
- **坑 5**：仓库 `core.autocrlf=true`，checkout 出的 `.sh` 会变 CRLF，bash 报 `$'\r': command not found`。已用根目录 `.gitattributes` 的 `*.sh text eol=lf` 解决，且必须与 `flash.sh` 同批入库。

### E1.1 编译告警基线

先统一启用：

```text
-Wall
-Wextra
-Wpedantic
```

原则：

- 先理解并清理自己代码中的 warning。
- 暂不默认启用 `-Werror`。
- 后续更严格 warning 在 E2 再评估，不一次性堆满 GCC 选项。

✅ **已完成** — 环境：同上

实现方式：`cmake/warnings.cmake` 定义 `embedded_warnings` INTERFACE 目标，由 platform 传递给各实验；根 `CMakeLists.txt` 中 `include(cmake/warnings.cmake)` 必须在 `add_subdirectory()` 之前，顺序不可调换。

实测证据（干净构建，`build.ninja` 实际 FLAGS）：

| 编译对象 | 实际选项 |
|---|---|
| `main.c`（自有） | `-std=c11 … -fdata-sections -Wall -Wextra -Wpedantic` |
| `system_stm32f4xx.c`（Cube） | 同上，带三个告警选项 |
| `startup_stm32f446xx.s`（汇编） | **无告警选项** ← `$<COMPILE_LANGUAGE:C,CXX>` 挡住 |

- 编译期告警 **0 条**；链接期告警 **4 条**（`nosys` stub，预期保留）；elf 11468 字节，与改动前一致，无回归。
- 三个选项不冗余，用探针文件实测分工：`-Wall` 抓未使用变量，`-Wextra` 抓未使用参数与有/无符号比较，`-Wpedantic` 需写出 GCC 扩展才触发（002 之后常见的是 `__attribute__`、`0b` 字面量）。
- `$<COMPILE_LANGUAGE:C,CXX>` 的逗号多语言写法需 CMake ≥ 3.15（文档 "Changed in version 3.15"）。本次把 `cmake_minimum_required` 从 3.20 提到 3.21，理由是 presets schema v3 的 `toolchainFile` 需要 3.21 —— 属补旧账，与 E1.1 无关。
- **那 4 条链接警告的定性**：来自 `--specs=nosys.specs` 的空 syscall 实现，`-Wall` 系列对其无效；且是虚警 —— 警告发出时链接器还没执行 `-Wl,--gc-sections`，而本项目从不调用这些 stub，它们会被回收、不进固件。真正解法是自己实现 syscall（`_write` 重定向到 UART 做 printf），属后续 UART 实验范围；**不要用链接选项压制**，那会同时盖掉真实的 undefined symbol。说明已写在 `platform/stm32f446ze/CMakeLists.txt` 的 `--specs=nosys.specs` 旁。
- **未验证项（如实记录）**：`common/CMakeLists.txt` 新增的 `target_link_libraries(common INTERFACE embedded_warnings)` 目前**无法验证是否生效** —— `common` 是空壳且不编译任何源文件，001 也没链接它。只能确认配置阶段不报错。等 `common/` 有真实源文件时才会显现，届时须回补验证。
- **遗留后果**：不带 `-Werror`，带告警的构建仍 `exit=0`。E2 做 CI 时不能靠退出码发现告警，必须 grep 构建日志。

### E1.2 固件产物与链接分析

构建后自动得到并理解：

- `.elf`：调试和符号信息的核心产物。
- `.map`：分析 section、symbol、object file 和链接来源。
- `.bin`：裸二进制部署格式。
- `.hex`：带地址信息的 Intel HEX 部署格式。
- `arm-none-eabi-size`：每次构建观察 Flash / RAM 占用。

重点把 `.map` 和已经学过的 ELF / linker script / `nm` 串起来，而不是只生成文件。

✅ **已完成** — 环境：同上（CMake 4.4.3 / Ninja 1.13.2）

实现方式：`cmake/firmware.cmake` 定义 `embedded_add_firmware_artifacts(<target>)`，根 CMakeLists 在 `add_subdirectory` 之前 include；001 显式调用一行。函数内做三件事：链接期 `-Wl,-Map` + `-Wl,--print-memory-usage`；POST_BUILD 用 objcopy 生成 `.bin`/`.hex` 并跑 `size`。产物名用 `$<TARGET_FILE_BASE_NAME:...>`，OUTPUT_NAME 改名也不脱节。板级 CMakeLists 未动。

与 E1.1 的模式差异（有意为之）：warnings 靠 platform 自动继承防漏配；产物的 POST_BUILD 命令挂不到 INTERFACE 库上，**无法自动继承**，只能每实验显式一行——新建固件实验必须带这一行，已写进函数注释与 001 的调用处。

工具链加固：`CMAKE_OBJCOPY/OBJDUMP/SIZE/GDB` 由裸名字改为 configure 期 `find_program` 解析绝对路径，并按用途分级校验：objcopy/size 是构建硬依赖（POST_BUILD 用到），缺失 FATAL_ERROR；objdump/gdb 只用于分析/调试，缺失仅 WARNING——精简工具链（不带 gdb）不应被挡在 configure 之外。

**坑（只对"已有构建目录"成立）**：旧版 `set(... CACHE FILEPATH)` 在 cache 里存的是裸名字；find_program 发现变量已有值就视为"已有结果"跳过搜索，旧 cache 的裸名字会被原样保留、永远不解析。全新 build 目录没有旧值，直接 `find_program(CMAKE_OBJCOPY ...)` 是正常搜索的。修法：查找前把 cache 里"非绝对路径"的旧值 `unset(... CACHE)` 清掉，再直接 find_program；不用 FORCE 回填——那会每次 configure 覆写用户 `-D` 指定的路径，破坏 find_program"尊重已有有效值"的语义。

**验证（四条路径实测）**：① 现有 cache 重配置、② 模拟旧版裸名字 cache 重配置、③ `--fresh` 全新配置——三者 cache 均为绝对路径，干净重建 size 仍 916/0/1568、elf 仍 11468 B；④ 屏蔽工具链目录的探针配置：objdump/gdb 缺失只出 2 条 WARNING、configure 正常通过，用户指定的 objcopy 绝对路径被保留。

实测证据（干净构建 + `--clean-first` 全量重建，两次一致）：

| 产物 | 大小 | 说明 |
|---|---|---|
| `.elf` | 11468 B | 与 E1.0/E1.1 一致，无回归 |
| `.bin` | **916 B** | 恰等于 `verified 916 bytes`（data=0，无填充） |
| `.hex` | 2663 B | 首条扩展地址记录 `:020000040800F2` → 基址 0x08000000 |
| `.map` | 80553 B | 含 Discarded input sections |
| size | 916 / 0 / 1568 | text/data/bss 与改动前一致 |
| memory-usage | ROM 916 B (0.17%) / RAM 1568 B (1.20%) | RAM 含 `._user_heap_stack` 预留 0x600 |

- **闭环 E1.1 的 nosys 虚警定性**：`.map` 的 Discarded input sections 里可见 `.text._close/_lseek/_read/_write`（各 0x10，来自 `libnosys.a`）连同 `.gnu.warning.*` 一起被 `--gc-sections` 回收；libc 的 `_close_r` 等包装也同样被回收。4 条链接警告确认不进固件。
- **elf 与 flash 差额**：11468 − 916 = 10552 B 为 section header、符号表、`.ARM.attributes` 等不写入 Flash 的部分；`.bin` 只取可加载段。
- 编译告警 0 条、链接虚警 4 条（预期保留），与 E1.1 记录一致。
- **未验证项（如实记录）**：本阶段只验证了产物生成与尺寸基线，未把 `.bin`/`.hex` 实际烧录上板（现有 flash 入口仍走 elf）；`--print-memory-usage` 的 RAM 口径含 heap/stack 预留，非运行时真实占用。

### E1.3 Debug / Release 构建模型

建立明确的 Debug / Release preset 或等价配置，理解：

- 调试信息 `-g`
- `-Og` 与调试体验
- `-O0 / -Og / -O2 / -Os` 对汇编、代码尺寸和调试行为的影响
- `NDEBUG` 等 Release 语义

✅ **已完成** — 环境：同上（CMake 4.4.3 / Ninja 1.13.2 / ARM GNU Toolchain 15.3.1）

实现方式（延续项目层策略模式，板级与实验构建逻辑零改动）：

- `cmake/build_type.cmake`：①单配置生成器守卫（多配置生成器直接拒绝，不静默失效）；②默认 Debug（尊重 preset/-D/旧 cache 已有值，不 FORCE）；③类型白名单 Debug/Release（拼错如 `Relase` 会静默套用空标志得到"四不像"固件，配置期即 FATAL_ERROR）；④显式定义 `CMAKE_{C,CXX,ASM}_FLAGS_{DEBUG,RELEASE}`，每次 configure 重置（此处 FORCE 是正确的：语义由本文件单点定义，与工具链文件"cache 是用户可指定的事实"不矛盾；用户临时选项走 `CMAKE_C_FLAGS`）。
- 语义：**Debug = -Og -g3**（-Og 为调试而优化，优于 -O0 的冗余栈操作；-g3 含宏信息）；**Release = -O2 -DNDEBUG**（不用 -Os/-O3，取舍留待有测量需求时评估；当前无 assert，NDEBUG 属语义声明）。ASM 单独接管：Debug 只加 -g，Release 置空——CMake 默认给 .s 塞 host 取向的 -g / -O3 -DNDEBUG，实测 build.ninja 发现后一并显式覆盖。
- `CMakePresets.json`：隐藏 base（生成器+工具链写一份）→ `stm32f446ze-debug` / `stm32f446ze-release`，**独立 binaryDir** `build/debug`、`build/release`——单配置生成器若共用目录，切类型会互相覆盖产物且 ninja 全量重编。另加 buildPresets `debug`/`release`。旧 preset 名 `stm32f446ze` 移除，README/注释已同步；`.gitignore` 的 `build-*/` 与 `build/` 已覆盖新目录。
- 旧 `build/` 根目录缓存（无 CMAKE_BUILD_TYPE）已清理，避免被新默认值静默改义；E1.0–E1.2 的实测记录保留原文不改写。

实测证据（两配置各自干净构建 + `--clean-first` 全量重建，结果一致）：

| 项 | Debug | Release |
|---|---|---|
| C FLAGS（build.ninja 实查） | `-Og -g3 -std=c11 …告警三件套` | `-O2 -DNDEBUG -std=c11 …告警三件套` |
| ASM FLAGS | `-g`（无优化概念） | 空（无 host 默认 -O3 噪声） |
| text/data/bss | **872 / 0 / 1568** | **872 / 0 / 1568** |
| .bin | 872 B | 872 B |
| .elf | **689104 B**（DWARF） | 11424 B |
| gdb `info line main` | `Line 6 of main.c starts at 0x8000250` ✅ 源码级 | `No line number information` ✅ 预期 |

- **调试信息不占 Flash 的实证**：Debug elf 比 Release 大 ~677 KB（.debug_* 节，readelf 可见 .debug_macro/.debug_str 占大头），但两者 .bin 同为 872 B——DWARF 全在非加载 section。
- **001 第 6 条源码级调试闭环（构建侧）**：`break main.c:行号`、step/next 所需的 DWARF 行号已具备；上板单步留待下次硬件会话顺手验证。
- 守卫实测：`-DCMAKE_BUILD_TYPE=Relase` 配置期报错；不带 preset 裸配置默认 Debug；两配置增量构建 `no work to do`；`flash_exp001_gpio_output` 在 debug 目录正常登记。
- **尺寸基线变更（有意，非回归）**：916 → 872 B。旧基线是"无优化标志"（等效 -O0），新 Debug 的 -Og 做了不碍调试的优化。两配置 .bin 哈希不同（872 B 同长不同内容），烧录时注意烧的是哪个目录。
- **新发现（记录）**：-g3 使链接器拉入 `libg_nano.a`（带断言的 newlib 变体）而非 `libc_nano.a`，nosys 4 条虚警的来源文件名随之变化，性质不变、仍被 GC 回收；Release 仍走 libc_nano.a。
- **未验证项（如实记录）**：上板源码级单步/断点、Release 版实际烧录运行；-Og 与 -O2 在本实验恰好同尺寸，不代表一般规律（代码量太小，优化空间有限）。

收尾复查（2026-11-19，全部实证）：

- **libg_nano 归因对照实验坐实**：隔离探针（同一 .c、同 -Og、同 specs，唯一变量 -g3）链接两次——带 -g3 选 `libg_nano.a`，不带选 `libc_nano.a`。机制：gcc 的 `-g` 会向链接器传 `--debug`，newlib 的 nano.specs 据此在带断言的 libg 与不带断言的 libc 之间切换。因此"虚警来源库随构建类型变化"是 -g 的确定性后果，非偶然。
- 两配置 .map 的 Discarded input sections 均确认：nosys 4 stub（各 0x10）+ `.gnu.warning.*` + libc 侧 `_close_r` 等包装全部被 GC 回收，虚警定性在两配置下同样成立。
- release 目录 `flash_exp001_gpio_output` 登记正常；`.gitignore` 的 `build/` 已覆盖 `build/debug`、`build/release`；文档无旧命令残留（E1.0 历史记录按原文保留）。
- 修正 build_type.cmake 两处过时注释（"清空再定义"实际为直接 FORCE；ASM 已由本文件接管），纯注释改动，不影响构建。

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
