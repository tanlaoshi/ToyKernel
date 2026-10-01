# toyos-common.sh — 解析 TOYOS_ROOT / Config；供 Scripts/*.sh source

toyos_die() {
    echo "error: $*" >&2
    exit 1
}

# $1 = 调用脚本路径（$0 或 BASH_SOURCE）
toyos_resolve_root() {
    local Caller="$1"
    local Here Root
    if [ -n "${TOYOS_ROOT:-}" ]; then
        Root="$(cd "$TOYOS_ROOT" && pwd)"
        if [ -d "$Root/ToyKernel" ] && { [ -d "$Root/Scripts" ] || [ -d "$Root/ToyKernel/OpenBox/Scripts" ]; }; then
            printf '%s\n' "$Root"
            return 0
        fi
        toyos_die "TOYOS_ROOT=$TOYOS_ROOT 无效（需含 ToyKernel/ 与 Scripts/）"
    fi
    Here="$(cd "$(dirname "$Caller")" && pwd)"
    # $TOYOS_ROOT/Scripts/*.sh
    if [ -d "$Here/../ToyKernel" ] && [ -d "$Here/../ToyBoot" ]; then
        Root="$(cd "$Here/.." && pwd)"
    # ToyKernel/OpenBox/Scripts/*.sh → 上三级到树根
    elif [ -d "$Here/../../../ToyKernel" ] && [ -d "$Here/../../../ToyBoot" ]; then
        Root="$(cd "$Here/../../.." && pwd)"
    # 兼容：Scripts 在其它一层
    elif [ -d "$Here/../../ToyKernel" ] && [ -d "$Here/../../ToyBoot" ]; then
        Root="$(cd "$Here/../.." && pwd)"
    else
        toyos_die "无法解析 TOYOS_ROOT（从 $Here）；请 export TOYOS_ROOT=…"
    fi
    if [ ! -d "$Root/ToyKernel" ]; then
        toyos_die "无法解析 TOYOS_ROOT（从 $Here）；请 export TOYOS_ROOT=…"
    fi
    printf '%s\n' "$Root"
}

toyos_arch_kernel() {
    case "$1" in
        x86|x86_64|X64|x64) echo x86_64 ;;
        arm64|aarch64|AA64) echo arm64 ;;
        riscv|riscv64|RISCV) echo riscv ;;
        *) echo "$1" ;;
    esac
}

toyos_load_config() {
    local Root="$1"
    DEFAULT_TARGET=toyos
    DEFAULT_ARCH=x86
    if [ -f "$Root/Config.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/Config.txt"
    elif [ -f "$Root/ToyKernel/OpenBox/Config.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/ToyKernel/OpenBox/Config.txt"
    fi
    if [ -f "$Root/Config.local.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/Config.local.txt"
    fi
}

toyos_scripts_dir() {
    local Root="$1"
    if [ -f "$Root/Scripts/build.sh" ]; then
        printf '%s\n' "$Root/Scripts"
    elif [ -f "$Root/ToyKernel/OpenBox/Scripts/build.sh" ]; then
        printf '%s\n' "$Root/ToyKernel/OpenBox/Scripts"
    else
        toyos_die "找不到 Scripts/build.sh（$Root/Scripts 或 OpenBox）"
    fi
}
