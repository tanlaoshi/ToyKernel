#!/bin/bash
# run-virt-riscv.sh — 薄入口 → Scripts/lib/run-virt-riscv.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/run-virt-riscv.sh" "$@"
