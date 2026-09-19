#!/bin/bash
set -e
cd "$(dirname "$0")"

# 用法:
#   ./build.sh              # ARCH=x86_64
#   ./build.sh DEBUG=1
#   ./build.sh SERIAL=0              # 总开关：不 Init UART、无串口 TX
#   ./build.sh SERIAL_USB=0          # 仅 USB/xHCI 串口 quiet
#   ./build.sh SCREEN_LOG=0          # boot GOP 不上字（串口仍可开）
#   ./build.sh SCREEN_LOG_SMP=1      # 屏上也打 SMP（默认不上）
#   ./build.sh NO_COM1=1             # 兼容旧名，等价 SERIAL=0
#   ./build.sh arm64        # PR-A7：完整 Common → KernelMain（默认 BRINGUP=0）
#   ./build.sh riscv
#   ./build.sh arm64 BRINGUP=1   # PR-A6：仅串口 hello
#   ./build.sh arm64 BOARD=virt
#   ./build.sh riscv BOARD=milk-v-duo-s
#   ./build.sh V=1          # 打印每条 gcc/ld；默认只留警告、错误和结果
ARCH=x86_64
BOARD=virt
DEBUG=0
LWIP=1
NO_COM1=0
SERIAL=1
SERIAL_BOOT=1
SERIAL_USB=1
SERIAL_SMP=1
SERIAL_GUI=1
SERIAL_NET=1
SERIAL_FS=1
SERIAL_MEM=1
SERIAL_DRV=1
SERIAL_MISC=1
SCREEN_LOG=1
SCREEN_LOG_BOOT=1
SCREEN_LOG_USB=1
SCREEN_LOG_SMP=0
SCREEN_LOG_GUI=1
SCREEN_LOG_NET=0
SCREEN_LOG_FS=1
SCREEN_LOG_MEM=0
SCREEN_LOG_DRV=0
SCREEN_LOG_MISC=0
TOY_DEMO_DRIVER=1
BRINGUP=
QUIET=1
for Arg in "$@"; do
    case "$Arg" in
        DEBUG=1|debug=1) DEBUG=1 ;;
        DEBUG=0|debug=0) DEBUG=0 ;;
        LWIP=1|lwip=1) LWIP=1 ;;
        LWIP=0|lwip=0) LWIP=0 ;;
        NO_COM1=1|no_com1=1) NO_COM1=1; SERIAL=0 ;;
        NO_COM1=0|no_com1=0) NO_COM1=0 ;;
        SERIAL=1|serial=1) SERIAL=1 ;;
        SERIAL=0|serial=0) SERIAL=0 ;;
        SERIAL_BOOT=*) SERIAL_BOOT="${Arg#SERIAL_BOOT=}" ;;
        SERIAL_USB=*) SERIAL_USB="${Arg#SERIAL_USB=}" ;;
        SERIAL_SMP=*) SERIAL_SMP="${Arg#SERIAL_SMP=}" ;;
        SERIAL_GUI=*) SERIAL_GUI="${Arg#SERIAL_GUI=}" ;;
        SERIAL_NET=*) SERIAL_NET="${Arg#SERIAL_NET=}" ;;
        SERIAL_FS=*) SERIAL_FS="${Arg#SERIAL_FS=}" ;;
        SERIAL_MEM=*) SERIAL_MEM="${Arg#SERIAL_MEM=}" ;;
        SERIAL_DRV=*) SERIAL_DRV="${Arg#SERIAL_DRV=}" ;;
        SERIAL_MISC=*) SERIAL_MISC="${Arg#SERIAL_MISC=}" ;;
        SCREEN_LOG=1|screen_log=1) SCREEN_LOG=1 ;;
        SCREEN_LOG=0|screen_log=0) SCREEN_LOG=0 ;;
        SCREEN_LOG_BOOT=*) SCREEN_LOG_BOOT="${Arg#SCREEN_LOG_BOOT=}" ;;
        SCREEN_LOG_USB=*) SCREEN_LOG_USB="${Arg#SCREEN_LOG_USB=}" ;;
        SCREEN_LOG_SMP=*) SCREEN_LOG_SMP="${Arg#SCREEN_LOG_SMP=}" ;;
        SCREEN_LOG_GUI=*) SCREEN_LOG_GUI="${Arg#SCREEN_LOG_GUI=}" ;;
        SCREEN_LOG_NET=*) SCREEN_LOG_NET="${Arg#SCREEN_LOG_NET=}" ;;
        SCREEN_LOG_FS=*) SCREEN_LOG_FS="${Arg#SCREEN_LOG_FS=}" ;;
        SCREEN_LOG_MEM=*) SCREEN_LOG_MEM="${Arg#SCREEN_LOG_MEM=}" ;;
        SCREEN_LOG_DRV=*) SCREEN_LOG_DRV="${Arg#SCREEN_LOG_DRV=}" ;;
        SCREEN_LOG_MISC=*) SCREEN_LOG_MISC="${Arg#SCREEN_LOG_MISC=}" ;;
        TOY_DEMO_DRIVER=*) TOY_DEMO_DRIVER="${Arg#TOY_DEMO_DRIVER=}" ;;
        V=1|v=1|VERBOSE=1|verbose=1) QUIET=0 ;;
        V=0|v=0|VERBOSE=0|verbose=0) QUIET=1 ;;
        BRINGUP=1|bringup=1) BRINGUP=1 ;;
        BRINGUP=0|bringup=0) BRINGUP=0 ;;
        BOARD=*) BOARD="${Arg#BOARD=}" ;;
        *) ARCH="$Arg" ;;
    esac
done

if [ -z "$BRINGUP" ]; then
    BRINGUP=0
fi

echo "Building ToyKernel for ARCH=$ARCH BOARD=$BOARD TOY_KERNEL_DEBUG=$DEBUG SERIAL=$SERIAL SCREEN_LOG=$SCREEN_LOG USB=$SERIAL_USB LWIP=$LWIP BRINGUP=$BRINGUP"

case "$ARCH" in
    x86_64) HAL_ARCH=X64 ;;
    arm64)  HAL_ARCH=Arm64 ;;
    riscv)  HAL_ARCH=RiscV ;;
    *)
        echo "error: unknown ARCH=$ARCH (expected x86_64|arm64|riscv)" >&2
        exit 1
        ;;
esac
ELF="Build/HAL/$HAL_ARCH/Kernel.elf"
USER_HELLO="Build/HAL/$HAL_ARCH/user/hello.elf"

MAKE=(make)
if [ "$QUIET" = 1 ]; then
    MAKE=(make -s)
fi
"${MAKE[@]}" clean ARCH="$ARCH" BOARD="$BOARD"
"${MAKE[@]}" ARCH="$ARCH" BOARD="$BOARD" DEBUG="$DEBUG" LWIP="$LWIP" BRINGUP="$BRINGUP" \
    NO_COM1="$NO_COM1" SERIAL="$SERIAL" \
    SERIAL_BOOT="$SERIAL_BOOT" SERIAL_USB="$SERIAL_USB" SERIAL_SMP="$SERIAL_SMP" \
    SERIAL_GUI="$SERIAL_GUI" SERIAL_NET="$SERIAL_NET" SERIAL_FS="$SERIAL_FS" \
    SERIAL_MEM="$SERIAL_MEM" SERIAL_DRV="$SERIAL_DRV" SERIAL_MISC="$SERIAL_MISC" \
    SCREEN_LOG="$SCREEN_LOG" \
    SCREEN_LOG_BOOT="$SCREEN_LOG_BOOT" SCREEN_LOG_USB="$SCREEN_LOG_USB" \
    SCREEN_LOG_SMP="$SCREEN_LOG_SMP" SCREEN_LOG_GUI="$SCREEN_LOG_GUI" \
    SCREEN_LOG_NET="$SCREEN_LOG_NET" SCREEN_LOG_FS="$SCREEN_LOG_FS" \
    SCREEN_LOG_MEM="$SCREEN_LOG_MEM" SCREEN_LOG_DRV="$SCREEN_LOG_DRV" \
    SCREEN_LOG_MISC="$SCREEN_LOG_MISC" \
    TOY_DEMO_DRIVER="$TOY_DEMO_DRIVER"

if [ ! -f "$ELF" ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build successful: $ELF (BOARD=$BOARD DEBUG=$DEBUG LWIP=$LWIP BRINGUP=$BRINGUP)"

# CI 只 checkout ToyKernel，无 ../ToyImage；有则同步演示 ELF，无则跳过
if [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ] && [ -d ../ToyImage ]; then
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
    cp User/mmapdemo.elf ../ToyImage/MMAPDEMO.ELF
    cp User/killdemo.elf ../ToyImage/KILLDEMO.ELF
    cp User/sigdemo.elf ../ToyImage/SIGDEMO.ELF
    cp User/windemo.elf ../ToyImage/WINDEMO.ELF
    cp User/guidemo.elf ../ToyImage/GUIDEMO.ELF
    cp User/blitdemo.elf ../ToyImage/BLITDEMO.ELF
    cp User/libcdemo.elf ../ToyImage/LIBCDEMO.ELF
    cp User/dirdemo.elf ../ToyImage/DIRDEMO.ELF
    cp User/netlibdemo.elf ../ToyImage/NETLIB.ELF
    echo "Copied HELLO/.../GUIDEMO/BLITDEMO/LIBCDEMO/DIRDEMO/NETLIB -> ../ToyImage/"
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
        cp -f ../ToyImage/MMAPDEMO.ELF ../ToyImage/rootfs/MMAPDEMO.ELF
        cp -f ../ToyImage/KILLDEMO.ELF ../ToyImage/rootfs/KILLDEMO.ELF
        cp -f ../ToyImage/SIGDEMO.ELF ../ToyImage/rootfs/SIGDEMO.ELF
        cp -f ../ToyImage/WINDEMO.ELF ../ToyImage/rootfs/WINDEMO.ELF
        cp -f ../ToyImage/GUIDEMO.ELF ../ToyImage/rootfs/GUIDEMO.ELF
        cp -f ../ToyImage/BLITDEMO.ELF ../ToyImage/rootfs/BLITDEMO.ELF
        cp -f ../ToyImage/LIBCDEMO.ELF ../ToyImage/rootfs/LIBCDEMO.ELF
        cp -f ../ToyImage/DIRDEMO.ELF ../ToyImage/rootfs/DIRDEMO.ELF
        cp -f ../ToyImage/NETLIB.ELF ../ToyImage/rootfs/NETLIB.ELF
        echo "Synced Kernel/HELLO/.../DIRDEMO/NETLIB -> ../ToyImage/rootfs/"
    fi
    echo "Copied $ELF -> ../ToyImage/"
elif [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ]; then
    echo "note: no ../ToyImage (CI) — skip demo ELF copy"
else
    echo "Non-x86 / bringup ELF (not copied to ToyImage): $ELF"
    if [ "$BRINGUP" = "0" ] && [ -f "$USER_HELLO" ]; then
        mkdir -p VirtRootFs
        cp -f "$USER_HELLO" VirtRootFs/HELLO.ELF
        echo "Copied $USER_HELLO -> VirtRootFs/HELLO.ELF"
    fi
fi

ls -lh "$ELF"
