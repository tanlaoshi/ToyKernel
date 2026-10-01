#!/bin/bash
# desktop.sh — desktop.sh dock-icon
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    dock-icon) (cd "$ROOT/ToyImage" && ./Scripts/dock-icon.sh "$@") ;;
    *) echo "usage: desktop.sh dock-icon" >&2; exit 1 ;;
esac
