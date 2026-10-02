#!/bin/bash
# test-fs.sh — 薄入口 → Scripts/lib/test-fs.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/test-fs.sh" "$@"
