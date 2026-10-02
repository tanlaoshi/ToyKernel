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

for F in edksetup.sh edksetup.bat License.txt License-History.txt \
         Maintainers.txt ReadMe.rst CONTRIBUTING.md pip-requirements.txt; do
    if [ -e "$SRC/$F" ]; then
        echo "  + $F"
        cp -a "$SRC/$F" "$DST/$F"
    fi
done

# ToyBoot 包：链到树内独立仓（改 Boot 只改 ~/ToyOS/ToyBoot）
ln -sfn "$(cd "$DST_ROOT/ToyBoot" && pwd)" "$DST/ToyBoot"
echo "  + ToyBoot → $DST_ROOT/ToyBoot"

# 确保无 .git
if [ -e "$DST/.git" ]; then
    rm -rf "$DST/.git"
fi
find "$DST" -name .git -type d -prune -exec rm -rf {} + 2>/dev/null || true

# README-TRIM
cat > "$DST/README-TRIM.md" <<EOF
# EDK2 裁剪说明（BOX-6）

> **无 \`.git\`**。版本钉 **EDK2 202408**（与备份源一致）。仅供 \`ToyBoot\` 编 \`BOOTX64.EFI\`。

## 保留

| 路径 | 用途 |
| ---- | ---- |
| \`edksetup.sh\` / \`Conf/\` / \`BaseTools/\` | 构建环境 |
| \`MdePkg/\` | \`Boot.dsc\` / \`Boot.inf\` 唯一包依赖 |
| \`ToyBoot/\` | **符号链接**到 \`\$TOYOS_ROOT/ToyBoot\`（独立 git） |

## 不保留

完整上游包树（OvmfPkg、MdeModulePkg、NetworkPkg、…）、备份源的 \`.git\`（约 1.6 GiB）。

## 如何再裁 / 再生成

\`\`\`bash
export TOYOS_ROOT=\$HOME/ToyOS
# 源 = 备份整树（含 edksetup）
bash "\$TOYOS_ROOT/ToyKernel/OpenBox/trim-edk2.sh" "\$EDK2_SRC" "\$TOYOS_ROOT"
source "\$TOYOS_ROOT/Scripts/env.sh"
build toyboot
\`\`\`

## Boot 契约（极简）

读 Kernel →（可选）设显示 → \`ExitBootServices\` → 跳入口。  
桌面 Logo / 清屏在 **Kernel**（BOX-2），不在 Boot。
EOF

echo "wrote $DST/README-TRIM.md"
echo "=========================================="
echo "DONE: $DST"
test ! -e "$DST/.git" && echo "check: no .git ✅"
du -sh "$DST" "$DST/MdePkg" "$DST/BaseTools" 2>/dev/null
echo "next: source Scripts/env.sh && build toyboot && toytest smoke"
echo "=========================================="
