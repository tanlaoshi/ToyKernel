#!/bin/bash
# migrate-to-toyos.sh — BOX-5：从现网 edk2 树拷出三仓到 ~/ToyOS（保留 edk2 备份）
# 用法：
#   ./OpenBox/migrate-to-toyos.sh [源edk2根] [目标根]
# 默认源 = OpenBox 上两级；目标 = $HOME/ToyOS
set -euo pipefail

OPENBOX="$(cd "$(dirname "$0")" && pwd)"
SRC_DEFAULT="$(cd "$OPENBOX/../.." && pwd)"
SRC="${1:-$SRC_DEFAULT}"
DST="${2:-${TOYOS_DST:-$HOME/ToyOS}}"

if [ ! -d "$SRC/ToyKernel/.git" ] || [ ! -d "$SRC/ToyBoot/.git" ] || [ ! -d "$SRC/ToyImage/.git" ]; then
    echo "error: 源 $SRC 需含 ToyKernel/ToyBoot/ToyImage（各自 .git）" >&2
    exit 1
fi
if [ ! -f "$SRC/edksetup.sh" ]; then
    echo "warn: $SRC 无 edksetup.sh — Boot 双轨需 EDK2_SRC 指向完整 EDK2 树" >&2
fi

echo "BOX-5 migrate: $SRC  →  $DST"
mkdir -p "$DST"

sync_repo() {
    local Name="$1"
    shift
    echo "==> rsync $Name"
    mkdir -p "$DST/$Name"
    rsync -a --delete \
        "$@" \
        "$SRC/$Name/" "$DST/$Name/"
}

# Kernel：排除巨型工具链与 Build；后再符号链接回源树（双轨共享）
sync_repo ToyKernel \
    --exclude '/Build/' \
    --exclude '/Tools/Extract/' \
    --exclude '/Tools/Tarballs/' \
    --exclude '/Tools/Root/' \
    --exclude '/Tools/Debs/'

mkdir -p "$DST/ToyKernel/Tools"
for Heavy in Extract Tarballs Root Debs; do
    if [ -e "$SRC/ToyKernel/Tools/$Heavy" ]; then
        ln -sfn "$SRC/ToyKernel/Tools/$Heavy" "$DST/ToyKernel/Tools/$Heavy"
        echo "    link Tools/$Heavy → 源树"
    fi
done

sync_repo ToyBoot
sync_repo ToyImage \
    --exclude '/Esp/X64/qemu_nvram*' \
    --exclude '/.qemu*/'

# Scripts + Config → $DST
export TOYOS_ROOT="$DST"
bash "$OPENBOX/install-to-root.sh"

# EDK2/：BOX-5 先符号链接到备份整树；BOX-6 再裁剪为无 .git 子集
if [ -f "$SRC/edksetup.sh" ]; then
    ln -sfn "$SRC" "$DST/EDK2"
    echo "linked EDK2 → $SRC （裁剪 → BOX-6）"
else
    echo "warn: $SRC 无 edksetup.sh，未创建 EDK2/ 链接" >&2
fi

# 双轨回退：Config.local 仍记 EDK2_SRC（无 EDK2/ 链接时用）
{
    echo "# BOX-5 本机覆盖（勿提交到三仓）"
    echo "# 优先用 \$TOYOS_ROOT/EDK2；下列为回退"
    echo "EDK2_SRC=$SRC"
} > "$DST/Config.local.txt"
echo "wrote $DST/Config.local.txt"

# 标记
{
    echo "ToyOS tree migrated from:"
    echo "  SRC=$SRC"
    echo "  date=$(date -Iseconds)"
    echo "  note=edk2 backup kept; EDK2 trim → BOX-6"
} > "$DST/MIGRATE.txt"

echo "=========================================="
echo "DONE: TOYOS_ROOT=$DST"
echo "  保留备份: $SRC"
echo "  next: source \"$DST/Scripts/env.sh\" && build && toytest smoke"
echo "=========================================="
