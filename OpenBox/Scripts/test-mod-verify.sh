#!/bin/bash
# test-mod-verify.sh — 薄入口 → Scripts/lib/test-mod-verify.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/test-mod-verify.sh" "$@"
