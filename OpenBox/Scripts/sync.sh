#!/bin/bash
# sync.sh — sync.sh <usb|nuc|kernel> …
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
SUB="${1:-}"
shift || true
case "$SUB" in
    usb) (cd "$ROOT/ToyImage" && ./Scripts/sync-usb.sh "$@") ;;
    nuc) (cd "$ROOT/ToyImage" && ./Scripts/sync-nuc.sh "$@") ;;
    kernel) (cd "$ROOT/ToyImage" && ./Scripts/sync-kernel-usb.sh "$@") ;;
    *) echo "usage: sync.sh <usb|nuc|kernel> [opts]" >&2; exit 1 ;;
esac
