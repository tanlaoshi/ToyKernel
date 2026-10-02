#!/bin/bash
# run.sh — run.sh [<x86|arm64|riscv>] [split|virt] …
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"
LIB="$(toyos_scripts_lib "$0")"

ARCH_IN="${DEFAULT_ARCH:-x86}"
MODE=""
ARGS=()
for A in "$@"; do
    case "$A" in
        x86|x86_64|X64|arm64|aarch64|riscv|riscv64) ARCH_IN="$A" ;;
        split|virt) MODE="$A" ;;
        *) ARGS+=("$A") ;;
    esac
done

KARCH="$(toyos_arch_kernel "$ARCH_IN")"
case "$KARCH" in
    x86_64)
        MODE="${MODE:-split}"
        echo "==> run $MODE (x86)"
        "$LIB/run-split.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    arm64)
        echo "==> run virt (arm64)"
        "$LIB/run-virt-arm.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    riscv)
        echo "==> run virt (riscv)"
        "$LIB/run-virt-riscv.sh" "${ARGS[@]+"${ARGS[@]}"}"
        ;;
    *) toyos_die "未知 arch: $ARCH_IN" ;;
esac
