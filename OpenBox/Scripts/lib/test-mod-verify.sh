#!/bin/bash
# test-mod-verify.sh — PR-MOD-app-verify：串跑 bundle expect
# 用法：从树根 ./Scripts/test-mod-verify.sh（或 test.sh mod-verify）
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/toyos-common.sh"
cd "$(toyos_image_root "$0")"
export TOY_SMP="${TOY_SMP:-1}"

need_expect() {
  if ! command -v expect >/dev/null 2>&1; then
    echo "error: need expect (sudo apt install expect)" >&2
    exit 1
  fi
}

run_one() {
  local Exp="$1"
  echo "==== $Exp ===="
  # 真源在 Scripts/lib；cwd=ToyImage；.exp spawn Scripts/lib/run-split.sh
  expect -f "$SCRIPT_DIR/$Exp"
}

need_expect
pkill -9 -f qemu-system-x86_64 2>/dev/null || true
run_one test-bundle-install.exp
run_one test-bundle-remove.exp
run_one test-bundle-combo.exp
echo "=== ALL PASS (mod-verify) ==="
