#!/bin/bash
# QEMU virt aarch64：PR-A7 KernelMain 或 PR-A6 hello
set -e
cd "$(dirname "$0")"

ELF="${1:-Build/arm64/Kernel.elf}"
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
PAT='ToyOS Arm64 virt: (KernelMain|hello)|ToyOS ready|ToyOS 就绪|sched: enter'
for _ in $(seq 1 60); do
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
echo "error: timeout waiting for Arm64 virt serial" >&2
exit 1
