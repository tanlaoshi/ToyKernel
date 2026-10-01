#!/bin/bash
# measure.sh — measure.sh boot [opts]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
SUB="${1:-boot}"
shift || true
case "$SUB" in
    boot) (cd "$ROOT/ToyImage" && ./Scripts/measure-boot.sh "$@") ;;
    *) echo "usage: measure.sh boot [opts]" >&2; exit 1 ;;
esac
