#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# scripts/flash.sh —— 把 ELF 烧进板子的唯一入口
#
# 用法：
#   bash scripts/flash.sh <openocd.cfg> <firmware.elf>
#
# CMake 的 flash_<target> 就是调用它；也可以手工执行：
#   bash scripts/flash.sh platform/stm32f446ze/openocd.cfg \
#                         build/experiments/001_gpio_output/exp001_gpio_output.elf
#
# 环境变量：
#   OPENOCD   指定 openocd 可执行文件（默认从 PATH 找 openocd）
#
# 平台：
#   Linux / macOS 原生可用；Windows 需要 Git Bash 或 MSYS2 的 bash。
#   WSL 的 bash 不行——它会按 Linux 路径解释 C:/... 这类参数。
# ---------------------------------------------------------------------------
set -euo pipefail

prog=$(basename "$0")

die() {
    printf '%s: %s\n' "$prog" "$*" >&2
    exit 1
}

if [ "$#" -ne 2 ]; then
    printf 'usage: %s <openocd.cfg> <firmware.elf>\n' "$prog" >&2
    exit 2
fi

cfg=$1
elf=$2

[ -f "$cfg" ] || die "openocd config not found: $cfg"
[ -f "$elf" ] || die "firmware not found: $elf"

openocd_bin=${OPENOCD:-openocd}
command -v "$openocd_bin" >/dev/null 2>&1 \
    || die "openocd not found (set OPENOCD=/path/to/openocd or add it to PATH)"

printf '%s: flashing %s\n' "$prog" "$elf" >&2

# 花括号是 Tcl 的字面量括号：路径里有空格也不会被拆开。
# program + verify + reset + exit：烧录、校验、复位并让 openocd 退出。
exec "$openocd_bin" -f "$cfg" -c "program {$elf} verify reset exit"
