#!/bin/bash
set -e
cd "$(dirname "$0")"

# 用法:
#   ./build.sh              # ARCH=x86_64
#   ./build.sh DEBUG=1
#   ./build.sh arm64        # PR-A7：完整 Common → KernelMain（默认 BRINGUP=0）
#   ./build.sh riscv
#   ./build.sh arm64 BRINGUP=1   # PR-A6：仅串口 hello
#   ./build.sh LWIP=1
ARCH=x86_64
DEBUG=0
LWIP=0
BRINGUP=
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
        LWIP=1|lwip=1) LWIP=1 ;;
        LWIP=0|lwip=0) LWIP=0 ;;
        BRINGUP=1|bringup=1) BRINGUP=1 ;;
        BRINGUP=0|bringup=0) BRINGUP=0 ;;
        *) ARCH="$Arg" ;;
    esac
done

if [ -z "$BRINGUP" ]; then
    BRINGUP=0
fi

echo "Building ToyKernel for ARCH=$ARCH TOY_DEBUG=$DEBUG LWIP=$LWIP BRINGUP=$BRINGUP"

make clean ARCH="$ARCH"
make ARCH="$ARCH" DEBUG="$DEBUG" LWIP="$LWIP" BRINGUP="$BRINGUP"

if [ "$ARCH" = "x86_64" ]; then
    ELF=Build/Kernel.elf
else
    ELF="Build/$ARCH/Kernel.elf"
fi

if [ ! -f "$ELF" ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: $ELF (DEBUG=$DEBUG LWIP=$LWIP BRINGUP=$BRINGUP)"

if [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ]; then
    cp Build/Kernel.elf ../ToyImage/
    cp User/hello.elf ../ToyImage/HELLO.ELF
    cp User/count.elf ../ToyImage/COUNT.ELF
    cp User/fork.elf ../ToyImage/FORK.ELF
    cp User/waitnh.elf ../ToyImage/WAITNH.ELF
    cp User/libtoy.so ../ToyImage/LIBTOY.SO
    cp User/dyndemo.elf ../ToyImage/DYNDEMO.ELF
    cp User/catfile.elf ../ToyImage/CAT.ELF
    cp User/writefile.elf ../ToyImage/WRITE.ELF
    cp User/netdemo.elf ../ToyImage/NETDEMO.ELF
    cp User/netsrv.elf ../ToyImage/NETSRV.ELF
    cp User/syshello.elf ../ToyImage/SYSHELLO.ELF
    cp User/sysfork.elf ../ToyImage/SYSFORK.ELF
    cp User/execdemo.elf ../ToyImage/EXECDEMO.ELF
    echo "Copied HELLO/COUNT/FORK/WAITNH/DYNDEMO/LIBTOY/CAT/WRITE/NETDEMO/NETSRV/SYSHELLO/SYSFORK/EXECDEMO -> ../ToyImage/"
    if [ -d ../ToyImage/rootfs ]; then
        cp -f ../ToyImage/Kernel.elf ../ToyImage/rootfs/Kernel.elf
        cp -f ../ToyImage/HELLO.ELF ../ToyImage/rootfs/HELLO.ELF
        cp -f ../ToyImage/CAT.ELF ../ToyImage/rootfs/CAT.ELF
        cp -f ../ToyImage/WRITE.ELF ../ToyImage/rootfs/WRITE.ELF
        cp -f ../ToyImage/WAITNH.ELF ../ToyImage/rootfs/WAITNH.ELF
        cp -f ../ToyImage/LIBTOY.SO ../ToyImage/rootfs/LIBTOY.SO
        cp -f ../ToyImage/DYNDEMO.ELF ../ToyImage/rootfs/DYNDEMO.ELF
        cp -f ../ToyImage/NETDEMO.ELF ../ToyImage/rootfs/NETDEMO.ELF
        cp -f ../ToyImage/NETSRV.ELF ../ToyImage/rootfs/NETSRV.ELF
        cp -f ../ToyImage/SYSHELLO.ELF ../ToyImage/rootfs/SYSHELLO.ELF
        cp -f ../ToyImage/SYSFORK.ELF ../ToyImage/rootfs/SYSFORK.ELF
        cp -f ../ToyImage/EXECDEMO.ELF ../ToyImage/rootfs/EXECDEMO.ELF
        echo "Synced Kernel/HELLO/CAT/WRITE/... -> ../ToyImage/rootfs/"
    fi
    echo "Copied Build/Kernel.elf -> ../ToyImage/"
else
    echo "Non-x86 / bringup ELF (not copied to ToyImage): $ELF"
fi

ls -lh "$ELF"
