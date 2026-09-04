#!/bin/bash
# QEMU virt riscv64：PR-V5 桌面 / V3–V4 输入+块 / V2 ramfb
# TOY_VIRT_GUI=1 开窗口；TOY_VIRT_SERIAL=1 纯串口子集；TOY_VIRT_NODISK=1 无盘
set -e
cd "$(dirname "$0")"

ELF="${1:-Build/riscv/Kernel.elf}"
MEM="${TOY_VIRT_MEM:-256M}"
if [ ! -f "$ELF" ]; then
    echo "building ARCH=riscv ..."
    make ARCH=riscv BRINGUP=0
    ELF=Build/riscv/Kernel.elf
fi

QEMU="${QEMU_RISCV64:-qemu-system-riscv64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
    if [ -x tools/root/usr/bin/qemu-system-riscv64 ]; then
        QEMU=tools/root/usr/bin/qemu-system-riscv64
    else
        echo "error: qemu-system-riscv64 not found" >&2
        exit 1
    fi
fi

./prepare-virt-rootfs.sh >/dev/null

OUT=$(mktemp)
cleanup() {
    if [ -n "${QPID:-}" ]; then
        kill -9 "$QPID" 2>/dev/null || true
        wait "$QPID" 2>/dev/null || true
    fi
    rm -f "$OUT"
}
trap cleanup EXIT

BIOS_ARGS=()
if [ "${TOY_RISCV_BIOS_NONE:-0}" = "1" ]; then
    BIOS_ARGS=(-bios none)
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
printf '\nhelp\nvols\nls\ncat THEME.CFG\nmem\nhalt\n' | "$QEMU" -M virt "${BIOS_ARGS[@]}" -m "$MEM" \
    "${SERIAL_ARGS[@]}" ${DISP_ARGS[@]+"${DISP_ARGS[@]}"} "${DEV_ARGS[@]}" \
    -kernel "$ELF" \
    >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 120); do
    if grep -q 'ToyOS RiscV virt: hello' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
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
echo "error: timeout waiting for RiscV virt V5" >&2
exit 1
