#!/bin/bash
# serve-store-lan.sh — 导出并 http.server（PR-LAN-store-0）
# 用法:
#   ./Tools/Scripts/serve-store-lan.sh [out-dir] [port]
# 默认 out=Build/store-lan port=8080；前台阻塞。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
Out="${1:-$ROOT/Build/store-lan}"
Port="${2:-8080}"

"$ROOT/Tools/Scripts/export-store-lan.sh" "$Out"
echo "serve-store-lan: http://0.0.0.0:$Port/  (Ctrl+C stop)"
echo "hint: store repo <this-host-LAN-IP>:$Port"
cd "$Out"
exec python3 -m http.server "$Port"
