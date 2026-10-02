#!/bin/bash
# trim-edk2.sh — BOX-6：从备份 edk2 裁出无 .git 的 Boot 子集到 $TOYOS_ROOT/EDK2
# 用法：
#   ./OpenBox/trim-edk2.sh [源edk2] [目标TOYOS根]
# 默认源 = EDK2_SRC 或 OpenBox 上两级；目标 = $TOYOS_ROOT 或 $HOME/ToyOS
set -euo pipefail

OPENBOX="$(cd "$(dirname "$0")" && pwd)"
SRC_DEFAULT="$(cd "$OPENBOX/../.." && pwd)"
DST_ROOT="${2:-${TOYOS_ROOT:-$HOME/ToyOS}}"
SRC="${1:-}"

if [ -z "$SRC" ]; then
    if [ -f "$DST_ROOT/Scripts/Config.local.txt" ]; then
        # shellcheck disable=SC1090
        . "$DST_ROOT/Scripts/Config.local.txt"
    elif [ -f "$DST_ROOT/Config.local.txt" ]; then
        # shellcheck disable=SC1090
        . "$DST_ROOT/Config.local.txt"
    fi
    if [ -n "${EDK2_SRC:-}" ] && [ -f "${EDK2_SRC}/edksetup.sh" ]; then
        SRC="$EDK2_SRC"
    else
        SRC="$SRC_DEFAULT"
    fi
fi

DST="$DST_ROOT/EDK2"

if [ ! -f "$SRC/edksetup.sh" ] || [ ! -d "$SRC/MdePkg" ] || [ ! -d "$SRC/BaseTools" ]; then
    echo "error: 源 $SRC 不是可用的 EDK2（需 edksetup.sh + MdePkg + BaseTools）" >&2
    exit 1
fi
if [ ! -d "$DST_ROOT/ToyBoot" ]; then
    echo "error: $DST_ROOT/ToyBoot 不存在（先 BOX-5 migrate）" >&2
    exit 1
fi

echo "BOX-6 trim-edk2: $SRC  →  $DST（无 .git）"

# 若原为指向整树的符号链接，先拆掉
if [ -L "$DST" ]; then
    rm -f "$DST"
elif [ -d "$DST" ]; then
    # 已有实体目录：清掉再铺（保留 Build 可选）
    find "$DST" -mindepth 1 -maxdepth 1 ! -name Build -exec rm -rf {} +
fi
mkdir -p "$DST"

copy_one() {
    local Rel="$1"
    echo "  + $Rel"
    if [ -d "$SRC/$Rel" ]; then
        mkdir -p "$DST/$Rel"
        rsync -a --delete \
            --exclude '.git/' \
            --exclude '.github/' \
            --exclude 'Build/' \
            "$SRC/$Rel/" "$DST/$Rel/"
    else
        cp -a "$SRC/$Rel" "$DST/$Rel"
    fi
}

# Boot.dsc 仅依赖 MdePkg；另需 BaseTools + Conf + edksetup
copy_one BaseTools
copy_one Conf
copy_one MdePkg

for F in edksetup.sh License.txt; do
    if [ -e "$SRC/$F" ]; then
        echo "  + $F"
        cp -a "$SRC/$F" "$DST/$F"
    fi
done

# ToyBoot 包：链到树内独立仓（改 Boot 只改 ~/ToyOS/ToyBoot）
ln -sfn "$(cd "$DST_ROOT/ToyBoot" && pwd)" "$DST/ToyBoot"
echo "  + ToyBoot → $DST_ROOT/ToyBoot"

# 二次瘦身：只留编 ToyBoot 所需（文档/测试/未用 Library/Brotli 多语言…）
echo "  - strip non-build bulk"
rm -rf "$DST/BaseTools/Tests" "$DST/BaseTools/UserManuals" \
       "$DST/BaseTools/Plugin/DebugMacroCheck/tests" \
       "$DST/BaseTools/Plugin/HostBasedUnitTestRunner" \
       "$DST/BaseTools/Plugin/CodeQL" \
       "$DST/BaseTools/Scripts/PackageDocumentTools" \
       "$DST/MdePkg/Test" "$DST/MdePkg/Library/MipiSysTLib"
rm -f "$DST/BaseTools/ReadMe.rst" "$DST/BaseTools/"*.bat \
      "$DST/BaseTools/toolsetup.bat" 2>/dev/null || true
BROT="$DST/BaseTools/Source/C/BrotliCompress/brotli"
if [ -d "$BROT" ]; then
    for D in js tests java csharp go research docs python scripts fetch-spec; do
        rm -rf "$BROT/$D"
    done
fi
KEEP='BaseLib|BaseMemoryLib|BaseDebugLibNull|BasePcdLibNull|BasePrintLib|UefiDevicePathLib|RegisterFilterLibNull|BaseStackCheckLib|UefiLib|UefiBootServicesTableLib|UefiRuntimeServicesTableLib|UefiApplicationEntryPoint|UefiMemoryAllocationLib'
if [ -d "$DST/MdePkg/Library" ]; then
    for D in "$DST/MdePkg/Library"/*; do
        [ -d "$D" ] || continue
        Base="$(basename "$D")"
        echo "$Base" | grep -Eq "^($KEEP)$" && continue
        rm -rf "$D"
    done
fi
# MdePkg.dec 勿再指向已删 Include
if [ -f "$DST/MdePkg/MdePkg.dec" ]; then
    python3 - "$DST/MdePkg/MdePkg.dec" <<'PY'
import pathlib, re, sys
p = pathlib.Path(sys.argv[1])
t = p.read_text()
t2 = re.sub(
    r"(?m)^(\[Includes\]\n)(?:  .+\n)*",
    r"\1  Include\n\n",
    t,
    count=1,
)
p.write_text(t2)
PY
fi
find "$DST" -type d -name '__pycache__' -prune -exec rm -rf {} + 2>/dev/null || true

# 确保无 .git
if [ -e "$DST/.git" ]; then
    rm -rf "$DST/.git"
fi
find "$DST" -name .git -type d -prune -exec rm -rf {} + 2>/dev/null || true

# README-TRIM
cat > "$DST/README-TRIM.md" <<EOF
# EDK2 裁剪说明（ToyBoot 专用）

> **无 \`.git\`**。仅供编 \`BOOTX64.EFI\`（\`ToyBoot/Boot.dsc\` → 只链 \`MdePkg\`）。

## 保留

| 路径 | 用途 |
| ---- | ---- |
| \`edksetup.sh\` / \`Conf/\` / \`BaseTools/\` | 构建环境 |
| \`MdePkg/Include\` + \`Boot.dsc\` 用到的 \`Library/*\` | 唯一包依赖 |
| \`ToyBoot/\` | **符号链接**到 \`\$TOYOS_ROOT/ToyBoot\` |
| \`License.txt\` / \`README-TRIM.md\` | 许可与本说明 |

## 已剔除

上游文档、Windows \`.bat\`、BaseTools 测试/手册、Brotli 多语言与测试数据、\`MdePkg/Test\`、未进 \`Boot.dsc\` 的 Library（含 MipiSysTLib）等。

## 再生成

\`\`\`bash
bash "\$TOYOS_ROOT/ToyKernel/OpenBox/trim-edk2.sh" "\$EDK2_SRC" "\$TOYOS_ROOT"
source "\$TOYOS_ROOT/Scripts/env.sh" && build toyboot
\`\`\`
EOF

echo "wrote $DST/README-TRIM.md"
echo "=========================================="
echo "DONE: $DST"
test ! -e "$DST/.git" && echo "check: no .git ✅"
du -sh "$DST" "$DST/MdePkg" "$DST/BaseTools" 2>/dev/null
echo "next: source Scripts/env.sh && build toyboot && toytest smoke"
echo "=========================================="
