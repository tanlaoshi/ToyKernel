# toyos-common.sh — 解析 TOYOS_ROOT / Config；供 Scripts/*.sh source

toyos_die() {
    echo "error: $*" >&2
    exit 1
}

# $1 = 调用脚本路径（$0 或 BASH_SOURCE）
toyos_resolve_root() {
    local Caller="$1"
    local Here Root Cand
    toyos_root_ok() {
        local R="$1"
        [ -d "$R/ToyKernel" ] && { [ -d "$R/Scripts" ] || [ -d "$R/ToyKernel/OpenBox/Scripts" ]; }
    }
    if [ -n "${TOYOS_ROOT:-}" ]; then
        Root="$(cd "$TOYOS_ROOT" && pwd)"
        if toyos_root_ok "$Root"; then
            printf '%s\n' "$Root"
            return 0
        fi
        toyos_die "TOYOS_ROOT=$TOYOS_ROOT 无效（需含 ToyKernel/ 与 Scripts|OpenBox）"
    fi
    Here="$(cd "$(dirname "$Caller")" && pwd)"
    # Scripts/ · Scripts/lib/ · OpenBox/Scripts/ · OpenBox/Scripts/lib/ · Image/Scripts/
    for Cand in \
        "$(cd "$Here/.." 2>/dev/null && pwd)" \
        "$(cd "$Here/../.." 2>/dev/null && pwd)" \
        "$(cd "$Here/../../.." 2>/dev/null && pwd)" \
        "$(cd "$Here/../../../.." 2>/dev/null && pwd)"
    do
        [ -n "$Cand" ] || continue
        if toyos_root_ok "$Cand"; then
            printf '%s\n' "$Cand"
            return 0
        fi
    done
    toyos_die "无法解析 TOYOS_ROOT（从 $Here）；请 export TOYOS_ROOT=…"
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
    EDK2_SRC="${EDK2_SRC:-}"
    if [ -f "$Root/Config.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/Config.txt"
    elif [ -f "$Root/ToyKernel/OpenBox/Config.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/ToyKernel/OpenBox/Config.txt"
    fi
    if [ -f "$Root/Scripts/Config.local.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/Scripts/Config.local.txt"
    elif [ -f "$Root/Config.local.txt" ]; then
        # shellcheck disable=SC1090
        . "$Root/Config.local.txt"
    fi
    export EDK2_SRC
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

# $1=调用脚本路径 → ToyImage 绝对路径
toyos_image_root() {
    printf '%s/ToyImage\n' "$(toyos_resolve_root "$1")"
}

# $1=调用脚本路径 → Scripts/lib 绝对路径
toyos_scripts_lib() {
    local Root
    Root="$(toyos_resolve_root "$1")"
    printf '%s/lib\n' "$(toyos_scripts_dir "$Root")"
}

# $1=TOYOS_ROOT $2=ToyKernel|ToyBoot → $TOYOS_ROOT/Build/<名>
toyos_component_build_dir() {
    printf '%s/Build/%s\n' "$1" "$2"
}

# Kernel 产物目录：有树根 → $TOYOS_ROOT/Build/ToyKernel；单仓 → $Ker/Build（或相对 Build）。
# $1=TOYOS_ROOT（可空） $2=ToyKernel 绝对路径（单仓时用）
toyos_kernel_build_dir() {
    local Root="${1:-}"
    local Ker="${2:-}"
    if [ -n "$Root" ]; then
        mkdir -p "$Root/Build/ToyKernel"
        printf '%s/Build/ToyKernel\n' "$Root"
        return 0
    fi
    if [ -n "$Ker" ]; then
        mkdir -p "$Ker/Build"
        printf '%s/Build\n' "$Ker"
        return 0
    fi
    printf 'Build\n'
}

# 去掉仓内旧 Build 符号链接；有内容则迁到 $TOYOS_ROOT/Build/ToyKernel。
# $1=TOYOS_ROOT $2=ToyKernel 仓绝对路径
toyos_drop_kernel_build_link() {
    local Root="$1"
    local Ker="$2"
    local Link="$Ker/Build"
    local Dest
    Dest="$(toyos_kernel_build_dir "$Root" "$Ker")"
    if [ -L "$Link" ]; then
        rm -f "$Link"
        return 0
    fi
    if [ -d "$Link" ] && [ "$Link" != "$Dest" ]; then
        if [ "$(ls -A "$Link" 2>/dev/null)" ]; then
            cp -a "$Link"/. "$Dest"/ 2>/dev/null || true
        fi
        rm -rf "$Link"
    fi
}

# Boot：保证 $TOYOS_ROOT/Build/ToyBoot 可见（已在根下则不动；否则链到 EDK2 产物目录）。
# $1=TOYOS_ROOT $2=EDK 侧 Build/ToyBoot 绝对路径（含 DEBUG_GCC/… 的父级）
toyos_link_boot_build() {
    local Root="$1"
    local EdkBoot="$2"
    local Dest
    Dest="$(toyos_component_build_dir "$Root" ToyBoot)"
    mkdir -p "$Root/Build"
    if [ ! -d "$EdkBoot" ]; then
        return 1
    fi
    EdkBoot="$(cd "$EdkBoot" && pwd)"
    if [ "$EdkBoot" = "$Dest" ]; then
        return 0
    fi
    if [ -L "$Dest" ] || [ ! -e "$Dest" ]; then
        ln -sfn "$EdkBoot" "$Dest"
        return 0
    fi
    # 已是真目录：不覆盖，仅提示
    echo "note: $Dest 已存在（非链接）；EDK 产物在 $EdkBoot" >&2
    return 0
}
