#!/bin/bash
# sync-usb.sh — 薄入口 → Scripts/lib/sync-usb.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/sync-usb.sh" "$@"
