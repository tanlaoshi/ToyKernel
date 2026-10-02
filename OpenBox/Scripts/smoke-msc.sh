#!/bin/bash
# smoke-msc.sh — 薄入口 → Scripts/lib/smoke-msc.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/smoke-msc.sh" "$@"
