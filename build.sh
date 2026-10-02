#!/bin/bash
set -e
cd "$(dirname "$0")"

# 产物：有 ToyOS 树根 → $TOYOS_ROOT/Build/ToyKernel（无仓内链接）；
# 单仓（无旁挂 ToyBoot/Scripts）仍用 ./Build（BOX-7）。
toyos_pick_root_for_build() {
    if [ -n "${TOYOS_ROOT:-}" ] && [ -d "${TOYOS_ROOT}/ToyKernel" ]; then
        (cd "${TOYOS_ROOT}" && pwd)
        return 0
    fi
    if [ -d ../ToyBoot ] && { [ -d ../Scripts ] || [ -f ../Config.txt ] || [ -d ../ToyImage ]; }; then
        (cd .. && pwd)
        return 0
    fi
    return 1
}

BUILDDIR=Build
if Root="$(toyos_pick_root_for_build)"; then
    export TOYOS_ROOT="$Root"
    # 去掉旧仓内 Build 符号链接 / 真目录（有内容则迁到树根）
    if [ -L Build ]; then
        rm -f Build
    elif [ -d Build ]; then
        mkdir -p "$Root/Build/ToyKernel"
        if [ "$(ls -A Build 2>/dev/null)" ]; then
            cp -a Build/. "$Root/Build/ToyKernel"/ 2>/dev/null || true
        fi
        rm -rf Build
    fi
    BUILDDIR="$Root/Build/ToyKernel"
    mkdir -p "$BUILDDIR"
    echo "BUILDDIR=$BUILDDIR"
fi
export BUILDDIR

# 用法:
#   ./build.sh              # ARCH=x86_64
#   ./build.sh DEBUG=1
#   ./build.sh SERIAL=0              # 总开关：不 Init UART、无串口 TX
#   ./build.sh SERIAL_USB=0          # 仅 USB/xHCI 串口 quiet
#   ./build.sh SCREEN_LOG=1          # 打开 boot GOP 上滚（默认关，省 4K blit）
#   ./build.sh SCREEN_LOG_SMP=1      # 屏上也打 SMP（需先 SCREEN_LOG=1）
#   ./build.sh NO_COM1=1             # 兼容旧名，等价 SERIAL=0
#   ./build.sh arm64        # PR-A7：完整 Common → KernelMain（默认 BRINGUP=0；无 LwIp 端口时自动 LWIP=0）
#   ./build.sh riscv
#   ./build.sh arm64 BRINGUP=1   # PR-A6：仅串口 hello
#   ./build.sh arm64 BOARD=virt
#   ./build.sh riscv BOARD=milk-v-duo-s
#   ./build.sh arm64 LWIP=0      # 显式关（默认已自动关）
#   ./build.sh V=1          # 打印每条 gcc/ld；默认只留警告、错误和结果
ARCH=x86_64
BOARD=virt
DEBUG=0
LWIP=1
LWIP_SET=0
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
SCREEN_LOG=0
# 下列为 SCREEN_LOG=1 时的课堂默认；总关时 Makefile/脚本会强制清零
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
        LWIP=1|lwip=1) LWIP=1; LWIP_SET=1 ;;
        LWIP=0|lwip=0) LWIP=0; LWIP_SET=1 ;;
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

case "$ARCH" in
    x86_64) HAL_ARCH=X64 ;;
    arm64)  HAL_ARCH=Arm64 ;;
    riscv)  HAL_ARCH=RiscV ;;
    *)
        echo "error: unknown ARCH=$ARCH (expected x86_64|arm64|riscv)" >&2
        exit 1
        ;;
esac

# virt arm64/riscv 尚无 HAL/*/LwIp/lwipopts.h；默认关 lwIP，避免误编挂掉
if [ "$LWIP" = "1" ] && [ ! -f "CodeA-HAL/$HAL_ARCH/LwIp/include/lwipopts.h" ]; then
    if [ "$LWIP_SET" = "1" ]; then
        echo "error: LWIP=1 but CodeA-HAL/$HAL_ARCH/LwIp/include/lwipopts.h missing (no port yet)" >&2
        echo "  use: ./build.sh $ARCH LWIP=0" >&2
        exit 1
    fi
    echo "note: ARCH=$ARCH has no LwIp port — using LWIP=0"
    LWIP=0
fi

echo "Building ToyKernel for ARCH=$ARCH BOARD=$BOARD TOY_KERNEL_DEBUG=$DEBUG SERIAL=$SERIAL SCREEN_LOG=$SCREEN_LOG USB=$SERIAL_USB LWIP=$LWIP BRINGUP=$BRINGUP"
ELF="$BUILDDIR/CodeA-HAL/$HAL_ARCH/Kernel.elf"
USER_HELLO="$BUILDDIR/CodeA-HAL/$HAL_ARCH/user/hello.elf"

MAKE=(make)
if [ "$QUIET" = 1 ]; then
    MAKE=(make -s)
fi
"${MAKE[@]}" clean ARCH="$ARCH" BOARD="$BOARD" BUILDDIR="$BUILDDIR"
"${MAKE[@]}" ARCH="$ARCH" BOARD="$BOARD" BUILDDIR="$BUILDDIR" DEBUG="$DEBUG" LWIP="$LWIP" BRINGUP="$BRINGUP" \
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

# CI 只 checkout ToyKernel，无 ../ToyImage；有则同步到 RootFs/$HAL
# PR-LAN-store-src：卷根仅 ROOTFS-ELF 白名单；商店在 Store/（废 Assets/Store + StoreCache）
if [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ] && [ -d ../ToyImage/RootFs/X64 ]; then
    DEST=../ToyImage/RootFs/X64
    IMAGE_STORE=../ToyImage/Store
    USER_OUT="$BUILDDIR/User"
    cp -f "$ELF" "$DEST/Kernel.elf"
    # Image 种子：Assets → Guest Assets/
    if [ -d ../ToyImage/Assets ]; then
        mkdir -p "$DEST/Assets"
        cp -a ../ToyImage/Assets/. "$DEST/Assets/"
        rm -rf "$DEST/Assets/Store"
    fi

    # 根白名单（Store/ROOTFS-ELF.md）
    cp -f "$USER_OUT/hello.elf" "$DEST/HELLO.ELF"
    cp -f "$USER_OUT/count.elf" "$DEST/COUNT.ELF"
    cp -f "$USER_OUT/fork.elf" "$DEST/FORK.ELF"
    cp -f "$USER_OUT/waitnh.elf" "$DEST/WAITNH.ELF"
    cp -f "$USER_OUT/libtoy.so" "$DEST/LIBTOY.SO"
    cp -f "$USER_OUT/dyndemo.elf" "$DEST/DYNDEMO.ELF"
    cp -f "$USER_OUT/syshello.elf" "$DEST/SYSHELLO.ELF"
    cp -f "$USER_OUT/sysfork.elf" "$DEST/SYSFORK.ELF"
    cp -f "$USER_OUT/execdemo.elf" "$DEST/EXECDEMO.ELF"
    cp -f "$USER_OUT/pipedemo.elf" "$DEST/PIPEDEMO.ELF"
    cp -f "$USER_OUT/threadsmoke.elf" "$DEST/THREADSMOKE.ELF"
    cp -f "$USER_OUT/pthreadsmoke.elf" "$DEST/PTHREADSMOKE.ELF"
    cp -f "$USER_OUT/threaddemo.elf" "$DEST/THREADDEMO.ELF"
    cp -f "$USER_OUT/enosysdemo.elf" "$DEST/ENOSYS.ELF"

    # 课堂预装目录包（非扁平）；仍禁止 Apps/*.ELF。Snake 不预装，须 store install。
    mkdir -p "$DEST/Apps/hello" "$DEST/Apps/guidemo" "$DEST/Apps/taskmgr"
    cp -f "$USER_OUT/hello.elf" "$DEST/Apps/hello/HELLO.ELF"
    cp -f "$USER_OUT/guidemo.elf" "$DEST/Apps/guidemo/GUIDEMO.ELF"
    cp -f "$USER_OUT/taskmgr.elf" "$DEST/Apps/taskmgr/TASKMGR.ELF"
    rm -f "$DEST/Apps/TASKMGR.ELF" "$DEST/Apps/CAT.ELF" "$DEST/Apps/SNAKE.ELF"
    rm -rf "$DEST/Apps/snake"

    PackStore() {
        local Id="$1" Src="$2" File="$3"
        mkdir -p "$IMAGE_STORE/packages/$Id" "$DEST/Store/packages/$Id"
        cp -f "$Src" "$IMAGE_STORE/packages/$Id/$File"
        if [ -f "$IMAGE_STORE/packages/$Id/PKG.TXT" ]; then
            cp -f "$IMAGE_STORE/packages/$Id/PKG.TXT" "$DEST/Store/packages/$Id/"
        fi
        cp -f "$Src" "$DEST/Store/packages/$Id/$File"
        if [ -d "$IMAGE_STORE/packages/$Id/Assets" ]; then
            rm -rf "$DEST/Store/packages/$Id/Assets"
            cp -a "$IMAGE_STORE/packages/$Id/Assets" "$DEST/Store/packages/$Id/"
        fi
    }
    PackStore hello "$USER_OUT/hello.elf" HELLO.ELF
    PackStore guidemo "$USER_OUT/guidemo.elf" GUIDEMO.ELF
    PackStore cat "$USER_OUT/catfile.elf" CAT.ELF
    PackStore taskmgr "$USER_OUT/taskmgr.elf" TASKMGR.ELF
    PackStore snake "$USER_OUT/snake.elf" SNAKE.ELF
    PackStore windemo "$USER_OUT/windemo.elf" WINDEMO.ELF
    PackStore blitdemo "$USER_OUT/blitdemo.elf" BLITDEMO.ELF
    # CHAT-4：chat 只入 ToyImage/Store（供 export-store-lan），不进 Guest DEST。
    mkdir -p "$IMAGE_STORE/packages/chat"
    cp -f "$USER_OUT/chat.elf" "$IMAGE_STORE/packages/chat/CHAT.ELF"
    if [ -f "$IMAGE_STORE/catalog.txt" ]; then
        mkdir -p "$DEST/Store"
        # Guest catalog 去掉 chat 行；源 catalog 仍含 chat（导出用）
        grep -v '^chat|' "$IMAGE_STORE/catalog.txt" > "$DEST/Store/catalog.txt" || true
    fi
    rm -rf "$DEST/Store/packages/chat" "$DEST/Apps/chat"
    if [ -d "$DEST/Store/packages/chat" ] || [ -d "$DEST/Apps/chat" ]; then
        echo "error: Guest still has chat after strip" >&2
        exit 1
    fi
    if grep -q '^chat|' "$DEST/Store/catalog.txt" 2>/dev/null; then
        echo "error: Guest catalog still lists chat" >&2
        exit 1
    fi
    # 废旧路径（store-src）
    rm -rf "$DEST/Assets/Store" "$DEST/StoreCache"

    # 清非白名单卷根陈货（入店类改走 Store/packages）
    rm -f "$DEST/CAT.ELF" "$DEST/WRITE.ELF" "$DEST/NETDEMO.ELF" "$DEST/NETSRV.ELF" \
        "$DEST/BRKDEMO.ELF" "$DEST/MMAPDEMO.ELF" "$DEST/KILLDEMO.ELF" "$DEST/SIGDEMO.ELF" \
        "$DEST/WINDEMO.ELF" "$DEST/GUIDEMO.ELF" "$DEST/BLITDEMO.ELF" "$DEST/LIBCDEMO.ELF" \
        "$DEST/SLEEPDEMO.ELF" "$DEST/SNAKE.ELF" "$DEST/TASKMGR.ELF" "$DEST/DIRDEMO.ELF" \
        "$DEST/CWDDEMO.ELF" "$DEST/NETLIB.ELF" "$DEST/SOCKDEMO.ELF"

    echo "Synced Kernel + root whitelist + Store/packages + Apps/<id> -> $DEST/"
elif [ "$ARCH" = "x86_64" ] && [ "$BRINGUP" = "0" ]; then
    echo "note: no ../ToyImage/RootFs/X64 (CI) — skip demo ELF copy"
else
    echo "Non-x86 / bringup ELF (not copied to ToyImage root): $ELF"
    if [ "$BRINGUP" = "0" ] && [ -f "$USER_HELLO" ]; then
        case "$ARCH" in
            arm64) HAL_DIR=Arm64 ;;
            riscv) HAL_DIR=RiscV ;;
            *) HAL_DIR= ;;
        esac
        if [ -n "$HAL_DIR" ]; then
            mkdir -p "../ToyImage/RootFs/$HAL_DIR"
            cp -f "$USER_HELLO" "../ToyImage/RootFs/$HAL_DIR/HELLO.ELF"
            echo "Copied $USER_HELLO -> ../ToyImage/RootFs/$HAL_DIR/HELLO.ELF"
        fi
    fi
fi

ls -lh "$ELF"
