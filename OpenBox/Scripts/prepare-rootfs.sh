#!/bin/bash
# prepare-rootfs.sh — 薄入口 → Scripts/lib/prepare-rootfs.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/prepare-rootfs.sh" "$@"
