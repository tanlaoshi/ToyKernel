#!/bin/bash
# QEMU virt aarch64：PR-V5 桌面 / V3–V4 输入+块 / V2 ramfb
#
# 默认：-nographic + ramfb → 内核走桌面模块表（gui）；串口仍 ToyOS ready
# 窗口：TOY_VIRT_GUI=1
# 纯串口子集（无 FB）：TOY_VIRT_SERIAL=1（不加 ramfb）
# 无盘：TOY_VIRT_NODISK=1
set -e
cd "$(dirname "$0")"

ELF="${1:-Build/arm64/Kernel.elf}"
MEM="${TOY_VIRT_MEM:-256M}"
DTB_ADDR=0x4a000000
if [ ! -f "$ELF" ]; then
    echo "building ARCH=arm64 ..."
    make ARCH=arm64 BRINGUP=0
    ELF=Build/arm64/Kernel.elf
fi

QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
    if [ -x tools/root/usr/bin/qemu-system-aarch64 ]; then
        QEMU=tools/root/usr/bin/qemu-system-aarch64
    else
        echo "error: qemu-system-aarch64 not found" >&2
        exit 1
    fi
fi

./prepare-virt-rootfs.sh >/dev/null

OUT=$(mktemp)
DTB=$(mktemp)
cleanup() {
    if [ -n "${QPID:-}" ]; then
        kill -9 "$QPID" 2>/dev/null || true
        wait "$QPID" 2>/dev/null || true
    fi
    rm -f "$OUT" "$DTB"
}
trap cleanup EXIT

"$QEMU" -M virt,dumpdtb="$DTB" -cpu cortex-a72 -m "$MEM" >/dev/null 2>&1 || true
if [ ! -s "$DTB" ]; then
    echo "error: dumpdtb failed" >&2
    exit 1
fi

DISP_ARGS=()
SERIAL_ARGS=(-nographic)
DEV_ARGS=(-device virtio-keyboard-device -device virtio-tablet-device)
if [ "${TOY_VIRT_SERIAL:-0}" != "1" ]; then
    DISP_ARGS=(-device ramfb)
fi
if [ "${TOY_VIRT_NODISK:-0}" != "1" ]; then
    DEV_ARGS+=(-drive "if=none,id=toyroot,format=raw,file=fat:rw:virt-rootfs"
               -device virtio-blk-device,drive=toyroot)
fi
if [ "${TOY_VIRT_GUI:-0}" = "1" ]; then
    SERIAL_ARGS=(-serial mon:stdio)
    if [ "${TOY_VIRT_SERIAL:-0}" = "1" ]; then
        echo "error: TOY_VIRT_GUI needs ramfb (unset TOY_VIRT_SERIAL)" >&2
        exit 1
    fi
    DISP_ARGS=(-device ramfb -display "${TOY_VIRT_DISPLAY:-gtk}")
fi

echo "run: $QEMU -M virt … ${SERIAL_ARGS[*]} ${DISP_ARGS[*]:-no-ramfb}"
printf 'help\nvols\nls\ncat THEME.CFG\nmem\nhalt\n' | "$QEMU" -M virt -cpu cortex-a72 -m "$MEM" \
    "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}" \
    -kernel "$ELF" \
    -device loader,addr=$DTB_ADDR,file="$DTB" \
    >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 120); do
    if grep -q 'ToyOS Arm64 virt: hello' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
    # V5 桌面：gui + ToyOS ready；串口子集：virt: serial shell
    if grep -q 'ToyOS ready' "$OUT" 2>/dev/null \
        || grep -q 'ToyOS 就绪' "$OUT" 2>/dev/null; then
        if grep -q '\[mod\] gui' "$OUT" 2>/dev/null \
            || grep -q 'virt: serial shell' "$OUT" 2>/dev/null; then
            cat "$OUT"
            exit 0
        fi
    fi
    if ! kill -0 "$QPID" 2>/dev/null; then
        break
    fi
    sleep 0.25
done
cat "$OUT"
echo "error: timeout waiting for Arm64 virt V5" >&2
exit 1
