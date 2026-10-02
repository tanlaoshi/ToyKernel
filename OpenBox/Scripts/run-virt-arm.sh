#!/bin/bash
# run-virt-arm.sh — 薄入口 → Scripts/lib/run-virt-arm.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/run-virt-arm.sh" "$@"
