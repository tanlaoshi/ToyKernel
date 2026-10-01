#!/bin/bash
# test.sh — test.sh <套件> [<arch|host>] …
# 套件: smoke | smoke-virt | smoke-install | smoke-msc | fs | user | shell | enosys | mod-verify | all | unit
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"

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
IMG="$ROOT/ToyImage/Scripts"

case "$SUITE" in
    smoke)
        case "$KARCH" in
            x86_64) (cd "$ROOT/ToyImage" && ./Scripts/smoke-boot.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
            arm64|riscv) (cd "$ROOT/ToyImage" && ./Scripts/smoke-virt.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
            *) toyos_die "smoke: 不支持 arch=$ARCH_IN" ;;
        esac
        ;;
    smoke-virt) (cd "$ROOT/ToyImage" && ./Scripts/smoke-virt.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    smoke-install) (cd "$ROOT/ToyImage" && ./Scripts/smoke-install.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    smoke-msc) (cd "$ROOT/ToyImage" && ./Scripts/smoke-msc.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    fs) (cd "$ROOT/ToyImage" && ./Scripts/test-fs.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    user) (cd "$ROOT/ToyImage" && ./Scripts/test-user.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    shell) (cd "$ROOT/ToyImage" && ./Scripts/test-shell.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    enosys) (cd "$ROOT/ToyImage" && ./Scripts/test-enosys.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
    mod-verify) (cd "$ROOT/ToyImage" && ./Scripts/test-mod-verify.sh "${ARGS[@]+"${ARGS[@]}"}") ;;
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
