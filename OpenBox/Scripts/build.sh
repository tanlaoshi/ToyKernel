#!/bin/bash
# build.sh — build.sh [<toyos|toyboot|sdk|all>] [<x86|arm64|riscv>] [KEY=VAL…]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
toyos_load_config "$ROOT"

TARGET="${DEFAULT_TARGET:-toyos}"
ARCH_IN="${DEFAULT_ARCH:-x86}"
ARGS=()
GOT_TARGET=0
GOT_ARCH=0

for A in "$@"; do
    case "$A" in
        *=*)
            ARGS+=("$A")
            ;;
        toyos|toyboot|sdk|all|kernel|boot)
            if [ "$GOT_TARGET" = 0 ]; then
                TARGET="$A"
                GOT_TARGET=1
            else
                ARGS+=("$A")
            fi
            ;;
        x86|x86_64|X64|x64|arm64|aarch64|AA64|riscv|riscv64|RISCV)
            if [ "$GOT_ARCH" = 0 ]; then
                ARCH_IN="$A"
                GOT_ARCH=1
            else
                ARGS+=("$A")
            fi
            ;;
        *)
            ARGS+=("$A")
            ;;
    esac
done

KARCH="$(toyos_arch_kernel "$ARCH_IN")"

build_toyos() {
    echo "==> ToyKernel ($KARCH) @ $ROOT/ToyKernel"
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/ToyKernel" && ./build.sh "$KARCH" "${ARGS[@]}")
    else
        (cd "$ROOT/ToyKernel" && ./build.sh "$KARCH")
    fi
}

build_toyboot() {
    echo "==> ToyBoot @ $ROOT/ToyBoot (EDK2_SRC=${EDK2_SRC:-auto})"
    export TOYOS_ROOT="$ROOT"
    if [ -n "${EDK2_SRC:-}" ]; then
        export EDK2_SRC
    fi
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/ToyBoot" && ./build.sh "${ARGS[@]}")
    else
        (cd "$ROOT/ToyBoot" && ./build.sh)
    fi
}

build_sdk() {
    local Sdk="$ROOT/ToyKernel/Tools/build-sdk.sh"
    [ -f "$Sdk" ] || toyos_die "无 $Sdk"
    echo "==> SDK"
    if [ "${#ARGS[@]}" -gt 0 ]; then
        (cd "$ROOT/ToyKernel" && bash Tools/build-sdk.sh "${ARGS[@]}")
    else
        (cd "$ROOT/ToyKernel" && bash Tools/build-sdk.sh)
    fi
}

case "$TARGET" in
    toyos|kernel) build_toyos ;;
    toyboot|boot) build_toyboot ;;
    sdk) build_sdk ;;
    all)
        build_toyos
        build_toyboot
        ;;
    *) toyos_die "未知目标: $TARGET（toyos|toyboot|sdk|all）" ;;
esac
