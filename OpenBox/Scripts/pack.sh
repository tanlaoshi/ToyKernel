#!/bin/bash
# pack.sh — pack.sh <id> <elf-path> [file-name]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"
ROOT="$(toyos_resolve_root "$0")"
bash "$ROOT/ToyKernel/Tools/Scripts/pack-app.sh" "$@"
