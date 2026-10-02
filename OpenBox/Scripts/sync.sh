#!/bin/bash
# sync.sh — sync.sh <usb|nuc|kernel> …
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
LIB="$(toyos_scripts_lib "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    usb) "$LIB/sync-usb.sh" "$@" ;;
    nuc) "$LIB/sync-nuc.sh" "$@" ;;
    kernel) "$LIB/sync-kernel-usb.sh" "$@" ;;
    *) echo "usage: sync.sh <usb|nuc|kernel> [opts]" >&2; exit 1 ;;
esac
