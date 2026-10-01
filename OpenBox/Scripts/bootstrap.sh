#!/bin/bash
# bootstrap.sh — 一键装宿主依赖（幂等；不写死用户名）
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$HERE/lib/toyos-common.sh"

ROOT="$(toyos_resolve_root "$0")"
echo "bootstrap: TOYOS_ROOT=$ROOT"

need_cmd() {
    command -v "$1" >/dev/null 2>&1
}

PKGS=()
need_cmd qemu-system-x86_64 || PKGS+=(qemu-system-x86)
need_cmd ovmf || true
need_cmd gcc || PKGS+=(build-essential)
need_cmd make || PKGS+=(make)
need_cmd python3 || PKGS+=(python3)
need_cmd expect || PKGS+=(expect)

if [ "${#PKGS[@]}" -eq 0 ]; then
    echo "bootstrap: 基础命令已齐（qemu/gcc/make/python3/expect 视本机而定）"
else
    echo "bootstrap: 建议安装: ${PKGS[*]}"
    if need_cmd apt-get && [ "$(id -u)" = 0 ]; then
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y "${PKGS[@]}"
    elif need_cmd apt-get; then
        echo "bootstrap: 请执行: sudo apt-get install -y ${PKGS[*]}"
    else
        echo "bootstrap: 非 apt 环境，请自行安装上述依赖"
    fi
fi

# 交叉工具链仍走 ToyKernel/Tools（不强制系统包）
if [ -x "$ROOT/ToyKernel/Tools/fetch-toolchain.sh" ]; then
    echo "bootstrap: 可选 — $ROOT/ToyKernel/Tools/fetch-toolchain.sh"
elif [ -d "$ROOT/ToyKernel/Tools/Extract" ]; then
    echo "bootstrap: 已有 Tools/Extract 工具链树"
fi

echo "bootstrap: 完成。下一步: source \"$ROOT/Scripts/env.sh\" && build"
