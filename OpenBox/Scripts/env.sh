#!/bin/bash
# env.sh — 须 source（对标 edksetup）。设 TOYOS_ROOT + 短名函数。
#   source Scripts/env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "error: 请 source 本脚本，例如: source Scripts/env.sh" >&2
    exit 1
fi

_ToyEnvDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib/toyos-common.sh
. "$_ToyEnvDir/lib/toyos-common.sh"

TOYOS_ROOT="$(toyos_resolve_root "${BASH_SOURCE[0]}")"
export TOYOS_ROOT
toyos_load_config "$TOYOS_ROOT"
_ToyScripts="$(toyos_scripts_dir "$TOYOS_ROOT")"

toyos_run() {
    local Name="$1"
    shift
    if [ ! -f "$_ToyScripts/$Name.sh" ]; then
        echo "error: 无 $_ToyScripts/$Name.sh" >&2
        return 1
    fi
    bash "$_ToyScripts/$Name.sh" "$@"
}

build() { toyos_run build "$@"; }
run() { toyos_run run "$@"; }
toytest() { toyos_run test "$@"; }
prepare-fs() { toyos_run prepare-fs "$@"; }
bootstrap() { toyos_run bootstrap "$@"; }
sync() { toyos_run sync "$@"; }
store() { toyos_run store "$@"; }
pack() { toyos_run pack "$@"; }
usb() { toyos_run usb "$@"; }
measure() { toyos_run measure "$@"; }
desktop() { toyos_run desktop "$@"; }

# 文档短名 test：仅交互 shell 定义，避免盖掉脚本里的 test builtin
if [[ $- == *i* ]]; then
    test() { toyos_run test "$@"; }
fi

echo "TOYOS_ROOT=$TOYOS_ROOT"
echo "Config: DEFAULT_TARGET=$DEFAULT_TARGET DEFAULT_ARCH=$DEFAULT_ARCH"
echo "短名: build run toytest|test prepare-fs bootstrap sync store pack usb measure desktop"
unset _ToyEnvDir
