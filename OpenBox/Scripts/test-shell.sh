#!/bin/bash
# test-shell.sh — 薄入口 → Scripts/lib/test-shell.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/test-shell.sh" "$@"
