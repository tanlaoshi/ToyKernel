#!/bin/bash
# desktop.sh — desktop.sh dock-icon
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
LIB="$(toyos_scripts_lib "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    dock-icon)
        # shellcheck source=/dev/null
        . "$LIB/dock-icon.sh"
        install_toyos_dock_icon "$@"
        ;;
    *) echo "usage: desktop.sh dock-icon" >&2; exit 1 ;;
esac
