#!/bin/bash
# install-to-root.sh — 把 OpenBox 骨架装到 $TOYOS_ROOT（默认 = ToyKernel 的上一级）
set -euo pipefail
OPENBOX="$(cd "$(dirname "$0")" && pwd)"
ROOT="${TOYOS_ROOT:-$(cd "$OPENBOX/../.." && pwd)}"
if [ ! -d "$ROOT/ToyKernel" ]; then
    echo "error: TOYOS_ROOT=$ROOT 无 ToyKernel" >&2
    exit 1
fi
mkdir -p "$ROOT/Scripts/lib"
cp -a "$OPENBOX/Scripts/." "$ROOT/Scripts/"
cp -f "$OPENBOX/Config.txt" "$ROOT/Config.txt"
chmod +x "$ROOT/Scripts/"*.sh
echo "installed Scripts + Config.txt → $ROOT"
echo "next: source \"$ROOT/Scripts/env.sh\" && build"
