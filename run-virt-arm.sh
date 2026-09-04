#!/bin/bash
# QEMU virt aarch64：PR-V1 DTB 内存 / PR-A9 串口 / PR-A6 hello
#
# ELF -kernel 时 QEMU 不把 DTB 放进 x0。脚本 dumpdtb 后用 loader 放到
# 0x4a000000（与 HAL/Arm64/Startup.c ARM64_VIRT_DTB_ADDR 一致）。
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

# 按当前 -m 生成与机器匹配的 FDT
"$QEMU" -M virt,dumpdtb="$DTB" -cpu cortex-a72 -m "$MEM" >/dev/null 2>&1 || true
if [ ! -s "$DTB" ]; then
    echo "error: dumpdtb failed" >&2
    exit 1
fi

echo "run: $QEMU -M virt -cpu cortex-a72 -m $MEM -nographic -kernel $ELF -device loader,addr=$DTB_ADDR,file=dtb"
printf 'help\nmem\nps\nhalt\n' | "$QEMU" -M virt -cpu cortex-a72 -m "$MEM" -nographic \
    -kernel "$ELF" \
    -device loader,addr=$DTB_ADDR,file="$DTB" \
    >"$OUT" 2>&1 &
QPID=$!
for _ in $(seq 1 80); do
    if grep -q 'ToyOS Arm64 virt: hello' "$OUT" 2>/dev/null; then
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
echo "error: timeout waiting for Arm64 virt serial" >&2
exit 1
