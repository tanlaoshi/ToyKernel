#!/bin/bash
# smoke-install.sh — 薄入口 → Scripts/lib/smoke-install.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/smoke-install.sh" "$@"
