#!/bin/bash
# usb.sh — usb.sh make [opts]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
SUB="${1:-make}"
shift || true
case "$SUB" in
    make) (cd "$ROOT/ToyImage" && ./Scripts/make-usb-stick.sh "$@") ;;
    *) echo "usage: usb.sh make [opts]" >&2; exit 1 ;;
esac
