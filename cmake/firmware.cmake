# ---------------------------------------------------------------------------
# 项目级固件产物策略（E1.2）
#
# 定义 embedded_add_firmware_artifacts(<target>)，为固件 target 追加：
#   .map  链接期生成：section / symbol / 来源文件 / GC 回收记录
#   .bin  链接后生成：裸二进制，等于真正写进 Flash 的字节
#   .hex  链接后生成：Intel HEX，带地址记录
#   size  每次链接后打印 Flash / RAM 占用（默认 Berkeley 口径：text/data/bss）
#   --print-memory-usage  链接期按 linker script 的 MEMORY 区域报告占用，
#         与 size 互补：ROM 区域可与烧录 verify 字节数对照；RAM 区域包含
#         ._user_heap_stack 预留的 heap/stack，比 data+bss 更贴近真实预算。
#
# 为什么放项目层 cmake/ 而不是 platform：
#   - .map/.bin/.hex/size 是工具链/产品范畴，与哪块板无关，任何固件 target
#     都能复用；host 测试不调用本函数即可。
#
# 为什么显式调用、而不是像 embedded_warnings 那样由 platform 自动继承：
#   - POST_BUILD 自定义命令只能挂在真实构建目标上，INTERFACE 库挂不上，
#     无法通过 platform 传递——这是结构性限制，不是疏忽。
#   - 因此【新建固件实验必须调用本函数一次】；
#     漏调用的后果只是少产物，不影响固件正确性。
#
# 产物文件名用 $<TARGET_FILE_BASE_NAME:...> 而非 ${target}：
#   OUTPUT_NAME 可能与 target 名不同，保证 .map/.bin/.hex 永远与 .elf 同名。
#
# 已知边界（记录，不在本阶段处理）：
#   - -Wl,-Map=路径 未做额外转义，构建目录含空格会被拆断；
#     当前 build 路径无空格。
#   - POST_BUILD 产物没有独立依赖追踪：手动删掉 .bin/.hex/.map 后 ninja
#     不会单独重建它们，ninja clean 也不会删除它们，需等 elf 重新链接。
#   - 本函数所有动作均为观察性（map/报告/格式转换），不参与代码生成，
#     不改变固件的 text/data/bss。
# ---------------------------------------------------------------------------

function(embedded_add_firmware_artifacts target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "embedded_add_firmware_artifacts: target '${target}' does not exist"
        )
    endif()

    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR
            "embedded_add_firmware_artifacts: '${target}' is ${_type}, expected EXECUTABLE"
        )
    endif()

    # 重复调用检测：与 stm32f446ze_add_flash_target 的防重风格一致
    get_target_property(_applied ${target} EMBEDDED_FIRMWARE_ARTIFACTS_APPLIED)
    if(_applied)
        message(FATAL_ERROR
            "embedded_add_firmware_artifacts: already applied to '${target}'"
        )
    endif()
    set_target_properties(${target} PROPERTIES EMBEDDED_FIRMWARE_ARTIFACTS_APPLIED TRUE)

    # ① 链接时生成 .map，并按 MEMORY 区域打印占用
    target_link_options(${target} PRIVATE
        "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.map"
        "-Wl,--print-memory-usage"
    )

    # ② 链接后生成 .bin / .hex 并打印 size
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM
        COMMENT "firmware artifacts: .bin / .hex / size for ${target}"
    )
endfunction()
