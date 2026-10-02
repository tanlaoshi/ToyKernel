#!/bin/bash
# sync-nuc.sh — 薄入口 → Scripts/lib/sync-nuc.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/sync-nuc.sh" "$@"
