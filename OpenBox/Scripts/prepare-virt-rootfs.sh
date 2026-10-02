#!/bin/bash
# prepare-virt-rootfs.sh — 薄入口 → Scripts/lib/prepare-virt-rootfs.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/prepare-virt-rootfs.sh" "$@"
