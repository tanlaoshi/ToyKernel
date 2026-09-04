#!/bin/bash
# PR-V6：Arm + RiscV virt 无头冒烟（CI）
# 自有 Boot 路径；不是 ToyImage/smoke-boot.sh（OVMF）。
set -e
cd "$(dirname "$0")"

echo "=== Arm64 virt --headless ==="
./run-virt-arm.sh --headless
echo "=== RiscV virt --headless ==="
./run-virt-riscv.sh --headless
echo "smoke-virt: PASS"
