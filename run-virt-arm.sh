#!/bin/bash
# QEMU virt aarch64：PR-A8 模块子集 / PR-A6 hello
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
# hello=A6；idle=A8（勿单认 ready/[mod]，会在 idle 前过早退出）
PAT='ToyOS Arm64 virt: hello|virt: idle loop'
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
echo "error: timeout waiting for Arm64 virt serial" >&2
exit 1
