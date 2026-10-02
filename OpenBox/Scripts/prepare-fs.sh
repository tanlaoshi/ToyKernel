#!/bin/bash
# prepare-fs.sh — prepare-fs.sh [<x86|arm64|riscv>]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"
LIB="$(toyos_scripts_lib "$0")"
ARCH_IN="${1:-${DEFAULT_ARCH:-x86}}"
KARCH="$(toyos_arch_kernel "$ARCH_IN")"

case "$KARCH" in
    x86_64)
        echo "==> prepare-rootfs (x86)"
        "$LIB/prepare-rootfs.sh"
        ;;
    arm64|riscv)
        echo "==> prepare-virt-rootfs ($KARCH)"
        export TOY_VIRT_MAKE_ARCH="$KARCH"
        "$LIB/prepare-virt-rootfs.sh"
        ;;
    *) toyos_die "prepare-fs: 未知 arch $ARCH_IN" ;;
esac
