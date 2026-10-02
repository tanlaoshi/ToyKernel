#!/bin/bash
# test-enosys.sh — 薄入口 → Scripts/lib/test-enosys.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/test-enosys.sh" "$@"
