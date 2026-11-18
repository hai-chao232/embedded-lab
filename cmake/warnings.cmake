# ---------------------------------------------------------------------------
# 项目级编译告警策略（E1.1）
#
# 为什么是独立的 INTERFACE 目标，而不是塞进 platform：
#   - 告警策略属于"项目代码质量约束"，不属于某一块板；
#   - 任何 target 都能复用它（实验、将来的 host 侧测试、第二块板）；
#   - E2 要在 CI 上对自有代码启用 -Werror 时，只改这一个文件。
#
# 为什么由 platform 传递，而不是每个实验自己 link：
#   - 实验只链接 platform 就自动继承，不会出现"新实验忘了加一行，
#     基线静默失效"的情况；
#   - 想给某个 target 局部放宽时，在该 target 上追加覆盖选项即可，
#     例如 target_compile_options(<tgt> PRIVATE -Wno-pedantic)。
#     GCC 后出现的选项覆盖先出现的，所以无需改动本文件。
#     注意：不存在"不链接 platform 就关掉告警"这种退出方式——
#     不链接 platform 会同时失去 -mcpu、startup、linker script，target 根本构建不出来。
#
# 为什么带 COMPILE_LANGUAGE：
#   - 这三个选项只对 C/C++ 有意义，不必传给汇编（startup_stm32f446xx.s）。
# ---------------------------------------------------------------------------

add_library(embedded_warnings INTERFACE)

target_compile_options(
    embedded_warnings
    INTERFACE
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wpedantic>
)

# 暂不启用 -Werror：GCC 升级会新增告警，-Werror 会把"升级工具链"变成
# "必须先改代码"。按 ROADMAP 留给 E2 在 CI 上对自有代码启用，届时在这里追加：
#   target_compile_options(embedded_warnings INTERFACE
#       $<$<COMPILE_LANGUAGE:C,CXX>:-Werror>)
#
# 更严格的一批（-Wconversion / -Wshadow / -Wcast-qual ...）同样在 E2 评估，
# 现在不一次性堆满，避免噪声掩盖真正的问题。
