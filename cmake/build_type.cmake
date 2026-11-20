# ---------------------------------------------------------------------------
# 项目级 Debug / Release 构建模型（E1.3）
#
# 与 warnings.cmake / firmware.cmake 同属项目层策略：构建类型是产品范畴，
# 与哪块板无关；根 CMakeLists 在 add_subdirectory 之前 include 本文件。
#
# 做四件事：
#   ① 单配置生成器守卫：本策略依赖 CMAKE_C_FLAGS_<CONFIG> 机制，
#      只对 Ninja / Makefiles 这类单配置生成器有意义；VS 等多配置生成器
#      有自己的一套（CMAKE_CONFIGURATION_TYPES），直接拒绝，不静默失效。
#   ② 默认 Debug：学习仓库日常以调试为主；显式固化进 cache，
#      不依赖"空字符串行为"这种隐式约定。已有值（preset / -D / 旧 cache）
#      一律尊重，绝不 FORCE 覆写——与工具链文件同一原则。
#   ③ 类型白名单：拼错的构建类型（如 Relase）会让 CMake 静默套用空的
#      CMAKE_C_FLAGS_RELASE，得到一个无优化无调试信息的"四不像"固件。
#      配置阶段直接报错，fail fast。
#   ④ 显式重定义 Debug / Release 的 C/CXX/ASM 标志（FORCE 覆写，见下方④）：
#      CMake 内置默认（Debug=-g，Release=-O3 -DNDEBUG）是 host 取向的，
#      这里按嵌入式语义重写。每次 configure 重置为同一组值保证幂等——
#      本文件所在根 CMakeLists 每次 configure 都执行，且不受 cache 旧值干扰。
#
# 语义选择（为什么是这些选项）：
#   Debug   = -Og -g3
#     -Og：GCC 专为调试设计的优化档——做不影响调试体验的优化，
#          变量/单步/断点行为接近源码；-O0 并非更好，它产生冗余栈操作，
#          反而让反汇编更难读。-g3：DWARF 含宏信息，GDB 可 print 宏。
#     不加 -DNDEBUG：assert 保持启用。
#   Release = -O2 -DNDEBUG
#     -O2：性能取向的稳妥默认。不用 -Os/-O3：尺寸/激进优化的取舍
#          留到有真实测量需求时再评估，不预设结论。
#     -DNDEBUG：标准 assert 语义开关（关闭断言）。当前代码没有 assert，
#          此宏现阶段无行为影响，属语义声明，为后续实验立好约定。
#   两种类型都【不】额外加链接选项：调试信息只存在于 .elf 的非加载
#   section，不占 Flash 一个字节；.bin/.hex 尺寸可直接验证这一点。
#
# 作用范围与边界：
#   - CMAKE_C_FLAGS_<CONFIG> 是目录作用域变量，在根目录设置后由所有
#     add_subdirectory 继承——自有代码与第三方（system_stm32f4xx.c、
#     startup .s）统一生效，与 E1.1 告警的覆盖范围一致。
#   - 汇编（.s，走 gcc 前端）不吃 CMAKE_C_FLAGS，由下方 CMAKE_ASM_FLAGS_<CONFIG>
#     单独接管：Debug 加 -g（源码级单步可进入 startup），Release 置空。
#   - 用户想临时加选项：用 -DCMAKE_C_FLAGS=...（所有类型共用的前缀），
#     不要直接改 CMAKE_C_FLAGS_DEBUG——本文件每次 configure 会重置它，
#     这是有意的：类型语义由本文件单点定义，不允许 cache 悄悄漂移。
# ---------------------------------------------------------------------------

# ① 单配置生成器守卫
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_is_multi_config)
    message(FATAL_ERROR
        "build_type.cmake: multi-config generator '${CMAKE_GENERATOR}' is not supported; "
        "use a single-config generator (Ninja / Unix Makefiles) via the presets")
endif()

# ② 默认 Debug（尊重已有值：preset、-D、旧 cache 都优先）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug / Release)" FORCE)
endif()

# ③ 类型白名单（大小写敏感，与 CMake 变量后缀约定一致）
set(_allowed_build_types Debug Release)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${_allowed_build_types})
if(NOT CMAKE_BUILD_TYPE IN_LIST _allowed_build_types)
    message(FATAL_ERROR
        "build_type.cmake: CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}' is not supported; "
        "allowed: ${_allowed_build_types}")
endif()

# ④ 显式定义类型语义。FORCE 在此是正确的：这四个变量的语义由本文件
#    单点定义，每次 configure 重置为同一组值（幂等），不允许 cache 里
#    残留的旧默认（host 取向的 -g / -O3）或手改值悄悄漂移。
#    与工具链文件"不 FORCE"不矛盾：那边 cache 是用户可指定的事实，
#    这边 cache 只是本策略的持久化输出。用户自定义选项走 CMAKE_C_FLAGS。
set(CMAKE_C_FLAGS_DEBUG     "-Og -g3"      CACHE STRING "C flags for Debug"     FORCE)
set(CMAKE_CXX_FLAGS_DEBUG   "-Og -g3"      CACHE STRING "CXX flags for Debug"   FORCE)
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG" CACHE STRING "C flags for Release"   FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "CXX flags for Release" FORCE)

# 汇编（.s 走 gcc 前端）不吃 CMAKE_C_FLAGS，有独立的 ASM 标志变量。
# CMake 默认会给它塞 host 取向的 -g / -O3 -DNDEBUG（实测 build.ninja 可见），
# 这里一并显式接管：Debug 只加 -g（汇编器调试信息，无优化概念）；
# Release 置空——手写 startup 没有可优化/可断言的内容，-O3 纯属噪声。
set(CMAKE_ASM_FLAGS_DEBUG   "-g" CACHE STRING "ASM flags for Debug"   FORCE)
set(CMAKE_ASM_FLAGS_RELEASE ""   CACHE STRING "ASM flags for Release" FORCE)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
