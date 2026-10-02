#!/bin/bash
# PR-V6：Arm + RiscV virt 无头冒烟（CI）— 入口在 ToyImage
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

BOARD=virt
export BOARD

echo "=== Arm64 virt --headless ==="
"$SCRIPT_DIR/run-virt-arm.sh" --headless
echo "=== RiscV virt --headless ==="
"$SCRIPT_DIR/run-virt-riscv.sh" --headless
echo "smoke-virt: PASS"
