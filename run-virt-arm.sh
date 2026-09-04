#!/bin/bash
# PR-A6：QEMU virt aarch64 -kernel 串口 hello
set -e
cd "$(dirname "$0")"

ELF="${1:-Build/arm64/Kernel.elf}"
if [ ! -f "$ELF" ]; then
    echo "building ARCH=arm64 BRINGUP=1 ..."
    make ARCH=arm64 BRINGUP=1
    ELF=Build/arm64/Kernel.elf
fi

QEMU="${QEMU_AARCH64:-qemu-system-aarch64}"
if ! command -v "$QEMU" >/dev/null 2>&1; then
    if [ -x tools/root/usr/bin/qemu-system-aarch64 ]; then
        QEMU=tools/root/usr/bin/qemu-system-aarch64
    else
        echo "error: qemu-system-aarch64 not found (apt install qemu-system-arm)" >&2
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

echo "run: $QEMU -M virt -cpu cortex-a72 -nographic -kernel $ELF"
"$QEMU" -M virt -cpu cortex-a72 -m 256M -nographic -kernel "$ELF" \
    < /dev/null >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 40); do
    if grep -q 'ToyOS Arm64 virt: hello' "$OUT" 2>/dev/null; then
        cat "$OUT"
        exit 0
    fi
    if ! kill -0 "$QPID" 2>/dev/null; then
        break
    fi
    sleep 0.25
done
cat "$OUT"
echo "error: timeout waiting for serial hello" >&2
exit 1
