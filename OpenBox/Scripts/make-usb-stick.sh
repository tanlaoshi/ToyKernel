#!/bin/bash
# make-usb-stick.sh — 薄入口 → Scripts/lib/make-usb-stick.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/make-usb-stick.sh" "$@"
