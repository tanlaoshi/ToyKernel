#!/bin/bash
# QEMU virt riscv64：PR-A8 模块子集 / PR-A6 hello（-bios none）
set -e
cd "$(dirname "$0")"

ELF="${1:-Build/riscv/Kernel.elf}"
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

echo "run: $QEMU -M virt -bios none -nographic -kernel $ELF"
"$QEMU" -M virt -bios none -m 256M -nographic -kernel "$ELF" \
    < /dev/null >"$OUT" 2>&1 &
QPID=$!
# hello=A6；idle=A8（勿单认 ready/[mod]，会在 idle 前过早退出）
PAT='ToyOS RiscV virt: hello|virt: idle loop'
for _ in $(seq 1 80); do
    if grep -qE "$PAT" "$OUT" 2>/dev/null; then
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
