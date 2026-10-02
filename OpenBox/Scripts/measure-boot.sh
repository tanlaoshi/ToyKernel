#!/bin/bash
# measure-boot.sh — 薄入口 → Scripts/lib/measure-boot.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "$HERE/lib/measure-boot.sh" "$@"
