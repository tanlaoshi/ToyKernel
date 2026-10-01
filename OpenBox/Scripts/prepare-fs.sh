#!/bin/bash
# prepare-fs.sh — prepare-fs.sh [<x86|arm64|riscv>]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"
ARCH_IN="${1:-${DEFAULT_ARCH:-x86}}"
KARCH="$(toyos_arch_kernel "$ARCH_IN")"

case "$KARCH" in
    x86_64)
        echo "==> prepare-rootfs (x86)"
        (cd "$ROOT/ToyImage" && ./Scripts/prepare-rootfs.sh)
        ;;
    arm64|riscv)
        echo "==> prepare-virt-rootfs ($KARCH)"
        (cd "$ROOT/ToyImage" && ./Scripts/prepare-virt-rootfs.sh)
        ;;
    *) toyos_die "prepare-fs: 未知 arch $ARCH_IN" ;;
esac
