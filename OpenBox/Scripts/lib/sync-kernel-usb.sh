#!/bin/bash
# sync-kernel-usb.sh — 兼容入口：转发到 sync-usb.sh --kernel-only
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/sync-usb.sh" --kernel-only "$@"
