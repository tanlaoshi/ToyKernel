#!/bin/bash
# smoke-boot.sh — 薄入口 → Scripts/lib/smoke-boot.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/smoke-boot.sh" "$@"
