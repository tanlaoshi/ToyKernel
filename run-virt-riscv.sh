#!/bin/bash
# QEMU virt riscv64：PR-V1 OpenSBI+DTB / PR-A9 串口
# 默认走 QEMU 自带 OpenSBI（内核 @0x80200000）。
# A6 hello 对照：TOY_RISCV_BIOS_NONE=1 时 -bios none（需内核仍链在 0x80000000 的旧产物）。
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
    echo "run: $QEMU -M virt -bios none -m $MEM -nographic -kernel $ELF"
else
    echo "run: $QEMU -M virt -m $MEM -nographic -kernel $ELF  # OpenSBI default"
fi

printf '\nhelp\nmem\nps\nhalt\n' | "$QEMU" -M virt "${BIOS_ARGS[@]}" -m "$MEM" -nographic -kernel "$ELF" \
    >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 80); do
    if grep -q 'ToyOS RiscV virt: hello' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
    if grep -q 'virt: serial shell' "$OUT" 2>/dev/null \
        && grep -q 'physical memory' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
    if ! kill -0 "$QPID" 2>/dev/null; then
        break
    fi
    sleep 0.25
done
cat "$OUT"
echo "error: timeout waiting for RiscV virt serial" >&2
exit 1
