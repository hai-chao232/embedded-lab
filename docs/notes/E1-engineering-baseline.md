# E1 工程基线笔记

> 上级：[ROADMAP](../../ROADMAP.md) · 相关：[cmake/README](../../cmake/README.md) · [001 README](../../experiments/001_gpio_output/README.md)

本文件保存 E1.0–E1.3 的详细实测、踩坑和未验证项；ROADMAP 只保留结论与入口。

## 一句话

E1 不是四个孤立配置，而是把 001 已经跑通的那条链路（源码 → ELF → 板子）逐段工程化：E1.0 打通最后一公里，E1.1 管住源码质量，E1.2 让产物可分析，E1.3 让同一份源码可切换调试/发布语义。

## 共同点与差异

- **共同点**：真实问题驱动；项目层单点；实验侧只表达意图；继承优先、显式兜底；数字、命令、踩坑都有证据。
- **差异**：E1.0 是 host → board；E1.1 是 source → object；E1.2 是 object → elf/bin/hex/map；E1.3 是同一源码 → 两种构建配置。只有 E1.3 改变了固件尺寸基线（916 → 872 B）。

## 详细记录

### E1.0 烧录入口

做什么：

- `platform/stm32f446ze/openocd.cfg` （板级 `source` 组合，实验不该知道调试器品牌）
- CMake `flash_<target>` ，用 `$<TARGET_FILE:...>` 传产物，不写死路径

验收：`ninja -C build flash_exp001_gpio_output` 能从干净状态一步烧录并复位（E1.3 后实际入口为 `build/debug`，此处保留当时的 `build`）。

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
- **001 第 6 条源码级调试闭环（构建侧）**：`break main.c:行号`、step/next 所需的 DWARF 行号已具备。
- 守卫实测：`-DCMAKE_BUILD_TYPE=Relase` 配置期报错；不带 preset 裸配置默认 Debug；两配置增量构建 `no work to do`；`flash_exp001_gpio_output` 在 debug 目录正常登记。
- **尺寸基线变更（有意，非回归）**：916 → 872 B。旧基线是"无优化标志"（等效 -O0），新 Debug 的 -Og 做了不碍调试的优化。两配置 .bin 哈希不同（872 B 同长不同内容），烧录时注意烧的是哪个目录。
- **新发现（记录）**：-g3 使链接器拉入 `libg_nano.a`（带断言的 newlib 变体）而非 `libc_nano.a`，nosys 4 条虚警的来源文件名随之变化，性质不变、仍被 GC 回收；Release 仍走 libc_nano.a。
- **未验证项（如实记录）**：Release 版实际烧录运行；-Og 与 -O2 在本实验恰好同尺寸，不代表一般规律（代码量太小，优化空间有限）。

### 收尾复查（2026-11-19，全部实证）

- **libg_nano 归因对照实验坐实**：隔离探针（同一 .c、同 -Og、同 specs，唯一变量 -g3）链接两次——带 -g3 选 `libg_nano.a`，不带选 `libc_nano.a`。机制：gcc 的 `-g` 会向链接器传 `--debug`，newlib 的 nano.specs 据此在带断言的 libg 与不带断言的 libc 之间切换。因此"虚警来源库随构建类型变化"是 -g 的确定性后果，非偶然。
- 两配置 .map 的 Discarded input sections 均确认：nosys 4 stub（各 0x10）+ `.gnu.warning.*` + libc 侧 `_close_r` 等包装全部被 GC 回收，虚警定性在两配置下同样成立。
- release 目录 `flash_exp001_gpio_output` 登记正常；`.gitignore` 的 `build/` 已覆盖 `build/debug`、`build/release`；文档无旧命令残留（E1.0 历史记录按原文保留）。
- 修正 build_type.cmake 两处过时注释（"清空再定义"实际为直接 FORCE；ASM 已由本文件接管），纯注释改动，不影响构建。

### 2026-11-20 上板补验

- **上板源码级调试已实证（2026-11-20 补验）**：flash 入口烧录 Debug 固件（Verified OK）后，OpenOCD+GDB 实测 `break main.c:6` 命中（硬件断点，Flash 只读地址自动选用）、next 逐行走过 13/16/17 行、`print/x RCC->AHB1ENR`=0x2（GPIOBEN）、`GPIOB->ODR`=0x1（LD1 点亮）——源码断点/单步/寄存器观察全链路可用，001 第 6 条闭环。

### 2026-11-20 GUI 调试工作流

- 环境实测：VS Code 扩展 Cortex-Debug 1.12.1 / CMake Tools 1.25.10 / cpptools 1.35.6 齐备；OpenOCD 经 ST-Link（V2J39M27）连接探测通过（SWD DPIDR 0x2ba01477，Cortex-M4 r0p1，6 硬件断点/4 观察点）。
- `.vscode/launch.json`：Cortex-Debug + 复用 `platform/stm32f446ze/openocd.cfg`（板级知识仍单点），executable 指向 build/debug 的 elf，`runToEntryPoint: main`；`.vscode/tasks.json`：preLaunchTask 走 `cmake --build --preset debug`，与命令行同一构建逻辑。设计上 F5 = 构建 + 下载 + 停在 main。
- 底层链路（OpenOCD+GDB）已无头实证源码级调试可用（见上）；**VS Code F5 界面侧待首次运行验证**（本次只验证了底层 OpenOCD+GDB 链路）。
- **待补**：STM32F446.svd（外设寄存器位域视图）仓库暂无，获取后在 launch.json 加 `svdFile`；OpenOCD/GDB/objdump 路径当前写死；flash 入口走 `OPENOCD` 环境变量/PATH，两处解析方式不同，E2 可复现环境时统一处理。
