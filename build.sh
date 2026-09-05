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

case "$ARCH" in
    x86_64) HAL_ARCH=X86_64 ;;
    arm64)  HAL_ARCH=Arm64 ;;
    riscv)  HAL_ARCH=RiscV ;;
    *)
        echo "error: unknown ARCH=$ARCH (expected x86_64|arm64|riscv)" >&2
        exit 1
        ;;
esac
ELF="Build/HAL/$HAL_ARCH/Kernel.elf"
USER_HELLO="Build/HAL/$HAL_ARCH/user/hello.elf"

make clean ARCH="$ARCH"
make ARCH="$ARCH" DEBUG="$DEBUG" LWIP="$LWIP" BRINGUP="$BRINGUP"

if [ ! -f "$ELF" ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: $ELF (DEBUG=$DEBUG LWIP=$LWIP BRINGUP=$BRINGUP)"

if [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ]; then
    cp "$ELF" ../ToyImage/
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
    cp User/pipedemo.elf ../ToyImage/PIPEDEMO.ELF
    cp User/brkdemo.elf ../ToyImage/BRKDEMO.ELF
    cp User/killdemo.elf ../ToyImage/KILLDEMO.ELF
    cp User/windemo.elf ../ToyImage/WINDEMO.ELF
    cp User/guidemo.elf ../ToyImage/GUIDEMO.ELF
    cp User/libcdemo.elf ../ToyImage/LIBCDEMO.ELF
    echo "Copied HELLO/.../GUIDEMO/LIBCDEMO -> ../ToyImage/"
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
        cp -f ../ToyImage/PIPEDEMO.ELF ../ToyImage/rootfs/PIPEDEMO.ELF
        cp -f ../ToyImage/BRKDEMO.ELF ../ToyImage/rootfs/BRKDEMO.ELF
        cp -f ../ToyImage/KILLDEMO.ELF ../ToyImage/rootfs/KILLDEMO.ELF
        cp -f ../ToyImage/WINDEMO.ELF ../ToyImage/rootfs/WINDEMO.ELF
        cp -f ../ToyImage/GUIDEMO.ELF ../ToyImage/rootfs/GUIDEMO.ELF
        cp -f ../ToyImage/LIBCDEMO.ELF ../ToyImage/rootfs/LIBCDEMO.ELF
        echo "Synced Kernel/HELLO/.../LIBCDEMO -> ../ToyImage/rootfs/"
    fi
    echo "Copied $ELF -> ../ToyImage/"
else
    echo "Non-x86 / bringup ELF (not copied to ToyImage): $ELF"
    if [ "$BRINGUP" = "0" ] && [ -f "$USER_HELLO" ]; then
        mkdir -p virt-rootfs
        cp -f "$USER_HELLO" virt-rootfs/HELLO.ELF
        echo "Copied $USER_HELLO -> virt-rootfs/HELLO.ELF"
    fi
fi

ls -lh "$ELF"
