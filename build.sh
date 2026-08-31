#!/bin/bash
set -e
cd "$(dirname "$0")"

# 用法:
#   ./build.sh              # ARCH=x86_64，关闭调试日志（默认）
#   ./build.sh DEBUG=1      # 打开 DebugWrite 串口日志
#   ./build.sh riscv        # 指定架构
#   ./build.sh riscv DEBUG=1

ARCH=x86_64
DEBUG=0
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
        *) ARCH="$Arg" ;;
    esac
done

echo "Building ToyKernel for ARCH=$ARCH TOY_DEBUG=$DEBUG"

make clean ARCH="$ARCH"
make ARCH="$ARCH" DEBUG="$DEBUG"

if [ ! -f Build/Kernel.elf ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: Build/Kernel.elf (DEBUG=$DEBUG)"
cp Build/Kernel.elf ../ToyImage/

if [ "$ARCH" = "x86_64" ]; then
    cp User/hello.elf ../ToyImage/HELLO.ELF
    cp User/count.elf ../ToyImage/COUNT.ELF
    echo "Copied User/hello.elf -> ../ToyImage/HELLO.ELF"
    echo "Copied User/count.elf -> ../ToyImage/COUNT.ELF"
fi

echo "Copied Build/Kernel.elf -> ../ToyImage/"
ls -lh Build/Kernel.elf
