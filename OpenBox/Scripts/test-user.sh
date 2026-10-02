#!/bin/bash
# test-user.sh — 薄入口 → Scripts/lib/test-user.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/test-user.sh" "$@"
