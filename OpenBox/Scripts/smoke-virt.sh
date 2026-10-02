#!/bin/bash
# smoke-virt.sh — 薄入口 → Scripts/lib/smoke-virt.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/smoke-virt.sh" "$@"
