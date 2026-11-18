# scripts/ 辅助脚本

> 上级：[根 README](../README.md)

烧录、调试、仪器辅助的自动化入口。脚本不依赖实验具体内容：通过参数传 elf / 串口设备等，不写死实验路径。

## flash.sh

```bash
bash scripts/flash.sh <openocd.cfg> <firmware.elf>
```

把 ELF 烧进板子、校验并复位。CMake 的 `flash_<target>` 就是调用它，手工调试时也可以直接跑。

- openocd 默认从 PATH 找，可用 `OPENOCD=/path/to/openocd` 覆盖。
- Windows 需要 Git Bash 或 MSYS2 的 bash；WSL 的 bash 不行（它按 Linux 路径解释 `C:/...`）。
- 脚本必须是 LF 行尾，规则在仓库根 `.gitattributes`（`*.sh text eol=lf`）。
