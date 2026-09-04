#!/bin/bash
# QEMU virt aarch64：PR-V3/V4 输入+块 / PR-V2 ramfb / PR-V1 DTB
#
# 默认：-nographic + ramfb + virtio-blk(fat:virt-rootfs) + virtio-keyboard/tablet
# 窗口：TOY_VIRT_GUI=1
# 无盘冒烟：TOY_VIRT_NODISK=1
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

DISP_ARGS=(-device ramfb)
SERIAL_ARGS=(-nographic)
DEV_ARGS=(-device virtio-keyboard-device -device virtio-tablet-device)
if [ "${TOY_VIRT_NODISK:-0}" != "1" ]; then
    DEV_ARGS+=(-drive "if=none,id=toyroot,format=raw,file=fat:rw:virt-rootfs"
               -device virtio-blk-device,drive=toyroot)
fi
if [ "${TOY_VIRT_GUI:-0}" = "1" ]; then
    SERIAL_ARGS=(-serial mon:stdio)
    DISP_ARGS+=(-display "${TOY_VIRT_DISPLAY:-gtk}")
fi

echo "run: $QEMU -M virt … ${SERIAL_ARGS[*]} ramfb + input + blk"
printf 'help\nvols\nls\ncat THEME.CFG\nmem\nhalt\n' | "$QEMU" -M virt -cpu cortex-a72 -m "$MEM" \
    "${SERIAL_ARGS[@]}" "${DISP_ARGS[@]}" "${DEV_ARGS[@]}" \
    -kernel "$ELF" \
    -device loader,addr=$DTB_ADDR,file="$DTB" \
    >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 120); do
    if grep -q 'ToyOS Arm64 virt: hello' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
    if grep -q 'virt: serial shell' "$OUT" 2>/dev/null \
        && grep -q '\[mod\] video' "$OUT" 2>/dev/null \
        && grep -q '\[mod\] fs' "$OUT" 2>/dev/null; then
        # 有盘时还要见到 vols / THEME
        if [ "${TOY_VIRT_NODISK:-0}" = "1" ] || grep -q 'THEME' "$OUT" 2>/dev/null \
            || grep -q 'TOYOS' "$OUT" 2>/dev/null; then
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
echo "error: timeout waiting for Arm64 virt V3/V4" >&2
exit 1
