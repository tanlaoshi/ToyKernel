#!/bin/bash
# sync-kernel-usb.sh — 薄入口 → Scripts/lib/sync-kernel-usb.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/sync-kernel-usb.sh" "$@"
