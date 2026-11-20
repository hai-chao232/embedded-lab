# cmake/ 工具链配置

> 上级：[根 README](../README.md)

交叉编译与构建模型的定义入口，放**工具链级**与**项目级构建策略**两类 CMake 配置；业务构建逻辑在各目录自己的 CMakeLists 中。

## 与构建入口的分工

| 文件 | 职责 |
|------|------|
| `CMakeLists.txt`（根） | 构建根入口：登记实验列表、组织 common / platform / experiments |
| `CMakePresets.json` | preset 入口：VS Code CMake Tools 与命令行共用同一套构建逻辑 |
| `cmake/arm-none-eabi.cmake` | 交叉编译工具链文件：编译器 / binutils 定位、裸机 System Name、交叉查找路径规则 |
| `cmake/warnings.cmake` | 项目级编译告警策略：定义 `embedded_warnings` INTERFACE 目标，与具体板无关，由 platform 传递给各实验 |
| `cmake/firmware.cmake` | 项目级固件产物策略（E1.2）：定义 `embedded_add_firmware_artifacts(<target>)`，生成 `.map/.bin/.hex` 并打印 size；由各实验显式调用 |
| `cmake/build_type.cmake` | 项目级 Debug / Release 构建模型（E1.3）：默认 Debug、类型白名单校验、显式定义各类型 C/CXX/ASM 标志（Debug=-Og -g3，Release=-O2 -DNDEBUG）；配合 presets 的独立构建目录 |

MCU 宏、FPU 与链接选项等**板级**参数在 `platform/<board>/` 内定义，不进工具链文件。
