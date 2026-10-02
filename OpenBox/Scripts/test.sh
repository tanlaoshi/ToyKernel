#!/bin/bash
# test.sh — test.sh <套件> [<arch|host>] …
# 套件: smoke | smoke-virt | smoke-install | smoke-msc | fs | user | shell | enosys | mod-verify | all | unit
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"
LIB="$(toyos_scripts_lib "$0")"
IMG="$ROOT/ToyImage"

SUITE="${1:-smoke}"
shift || true
ARCH_IN="${DEFAULT_ARCH:-x86}"
ARGS=()
for A in "$@"; do
    case "$A" in
        x86|x86_64|X64|arm64|aarch64|riscv|riscv64|host) ARCH_IN="$A" ;;
        *) ARGS+=("$A") ;;
    esac
done
KARCH="$(toyos_arch_kernel "$ARCH_IN")"

case "$SUITE" in
    smoke)
        case "$KARCH" in
            x86_64) "$LIB/smoke-boot.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
            arm64|riscv) "$LIB/smoke-virt.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
            *) toyos_die "smoke: 不支持 arch=$ARCH_IN" ;;
        esac
        ;;
    smoke-virt) "$LIB/smoke-virt.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
    smoke-install) "$LIB/smoke-install.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
    smoke-msc) "$LIB/smoke-msc.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
    fs|user|shell|enosys)
        # 真源 Scripts/lib；cwd=ToyImage；run-split 走 Scripts/lib
        (cd "$IMG" && "$LIB/test-${SUITE}.sh" "${ARGS[@]+"${ARGS[@]}"}")
        ;;
    mod-verify) "$LIB/test-mod-verify.sh" "${ARGS[@]+"${ARGS[@]}"}" ;;
    unit|all)
        T="$ROOT/ToyKernel/Tools/Scripts"
        if [ "$SUITE" = unit ]; then
            bash "$T/runtests.sh" "${ARGS[@]+"${ARGS[@]}"}"
        else
            bash "$T/test-all.sh" "${ARGS[@]+"${ARGS[@]}"}"
        fi
        ;;
    *) toyos_die "未知套件: $SUITE" ;;
esac
