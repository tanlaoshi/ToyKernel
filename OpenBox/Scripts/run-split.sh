#!/bin/bash
# run-split.sh — 薄入口 → Scripts/lib/run-split.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/run-split.sh" "$@"
