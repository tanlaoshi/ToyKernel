#!/bin/bash
set -e
cd "$(dirname "$0")"

# 用法:
#   ./build.sh              # ARCH=x86_64，关闭调试日志（默认）
#   ./build.sh DEBUG=1      # 打开 DebugWrite 串口日志
#   ./build.sh riscv        # 指定架构
#   ./build.sh riscv DEBUG=1

#   ./build.sh LWIP=1      # 嵌入 lwIP（需 ThirdParty/lwip）
ARCH=x86_64
DEBUG=0
LWIP=0
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
        LWIP=1|lwip=1) LWIP=1 ;;
        LWIP=0|lwip=0) LWIP=0 ;;
        *) ARCH="$Arg" ;;
    esac
done

echo "Building ToyKernel for ARCH=$ARCH TOY_DEBUG=$DEBUG LWIP=$LWIP"

make clean ARCH="$ARCH"
make ARCH="$ARCH" DEBUG="$DEBUG" LWIP="$LWIP"

if [ ! -f Build/Kernel.elf ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: Build/Kernel.elf (DEBUG=$DEBUG LWIP=$LWIP)"
cp Build/Kernel.elf ../ToyImage/

if [ "$ARCH" = "x86_64" ]; then
    cp User/hello.elf ../ToyImage/HELLO.ELF
    cp User/count.elf ../ToyImage/COUNT.ELF
    cp User/fork.elf ../ToyImage/FORK.ELF
    cp User/waitnh.elf ../ToyImage/WAITNH.ELF
    cp User/libtoy.so ../ToyImage/LIBTOY.SO
    cp User/dyndemo.elf ../ToyImage/DYNDEMO.ELF
    cp User/catfile.elf ../ToyImage/CAT.ELF
    cp User/writefile.elf ../ToyImage/WRITE.ELF
    echo "Copied HELLO/COUNT/FORK/WAITNH/DYNDEMO/LIBTOY/CAT/WRITE -> ../ToyImage/"
    if [ -d ../ToyImage/rootfs ]; then
        cp -f ../ToyImage/WRITE.ELF ../ToyImage/rootfs/WRITE.ELF
        cp -f ../ToyImage/WAITNH.ELF ../ToyImage/rootfs/WAITNH.ELF
        cp -f ../ToyImage/LIBTOY.SO ../ToyImage/rootfs/LIBTOY.SO
        cp -f ../ToyImage/DYNDEMO.ELF ../ToyImage/rootfs/DYNDEMO.ELF
        echo "Synced WRITE/WAITNH/DYNDEMO/LIBTOY -> ../ToyImage/rootfs/"
    fi
fi

echo "Copied Build/Kernel.elf -> ../ToyImage/"
ls -lh Build/Kernel.elf
