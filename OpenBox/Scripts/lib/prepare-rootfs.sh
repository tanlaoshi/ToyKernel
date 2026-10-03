#!/bin/bash
# 准备第二盘 RootFs/X64/（TOYOS 系统卷）
# 规范：Kernel.elf / THEME.CFG / 用户 ELF 只在 RootFs/X64；ESP 在 Esp/X64。
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
IMAGE_ROOT="$(toyos_image_root "$0")"
cd "$IMAGE_ROOT"

ROOT=RootFs/X64
mkdir -p "$ROOT"

BUILD_KERNEL="$(toyos_kernel_build_dir "$(toyos_resolve_root "$0")")/HAL/X64/Kernel.elf"
if [ ! -f "$BUILD_KERNEL" ] && [ ! -f "$ROOT/Kernel.elf" ]; then
    echo "Missing Kernel.elf — build ToyKernel first (→ $ROOT/)" >&2
    exit 1
fi

# 内核：优先 Build 产物
KERNEL_SRC=
if [ -f "$BUILD_KERNEL" ]; then
    KERNEL_SRC="$BUILD_KERNEL"
elif [ -f "$ROOT/Kernel.elf" ]; then
    KERNEL_SRC="$ROOT/Kernel.elf"
fi
if [ -n "$KERNEL_SRC" ] && [ "$KERNEL_SRC" != "$ROOT/Kernel.elf" ]; then
    if [ ! -f "$ROOT/Kernel.elf" ] || [ "$KERNEL_SRC" -nt "$ROOT/Kernel.elf" ] ||
       ! cmp -s "$KERNEL_SRC" "$ROOT/Kernel.elf" 2>/dev/null; then
        cp -f "$KERNEL_SRC" "$ROOT/Kernel.elf"
        if [ "${TOY_QEMU_VERBOSE:-0}" = 1 ]; then echo "Prepared kernel from $KERNEL_SRC"; fi
    fi
fi

# 主题：RootFs/X64/THEME.CFG 是唯一权威
if [ -f "$ROOT/theme.cfg" ]; then
    if [ ! -f "$ROOT/THEME.CFG" ]; then
        cp -f "$ROOT/theme.cfg" "$ROOT/THEME.CFG"
    fi
    rm -f "$ROOT/theme.cfg"
fi
if [ ! -f "$ROOT/THEME.CFG" ]; then
    # 无 mode=：真机 Keep 固件分辨率（PR-BOOT-fast-4）；QEMU 缺 mode= 时 edid 默认 1920x1080
    cat > "$ROOT/THEME.CFG" <<'EOF'
desktop=808080
shell=c0c0c0
font=2
scale=100
fade=6
wallpaper=0
theme=default
deskgrad=0
theme.effects=low
EOF
    if [ "${TOY_QEMU_VERBOSE:-0}" = 1 ]; then echo "Prepared THEME.CFG -> $ROOT/ (no mode=, solid grey)"; fi
fi
chmod u+rw "$ROOT/THEME.CFG" "$ROOT/TOYOS.DB" 2>/dev/null || true
if [ -f "$ROOT/THEME.CFG" ] && [ ! -w "$ROOT/THEME.CFG" ]; then
    echo "warning: $ROOT/THEME.CFG not writable by $(id -un) — Settings resolution will not persist" >&2
fi

# PR-UI-ttf-0：种子缺 CJK.TTF 时从仓库 stub 拷入（或生成）
SEED="$(toyos_resolve_root "$0")/ToyKernel/Tools/Fonts/CJK.TTF"
GEN="$(toyos_resolve_root "$0")/ToyKernel/Tools/Scripts/gen-cjk-ttf-stub.py"
mkdir -p Assets/Fonts
if [ ! -f Assets/Fonts/CJK.TTF ]; then
    if [ -f "$SEED" ]; then
        cp -f "$SEED" Assets/Fonts/CJK.TTF
    elif [ -f "$GEN" ]; then
        python3 "$GEN" || echo "warning: gen-cjk-ttf-stub failed — Boot: ttf miss" >&2
        [ -f "$SEED" ] && cp -f "$SEED" Assets/Fonts/CJK.TTF
    fi
fi

# 运行时资源 — 公共 Assets/ 种子 → RootFs/X64/Assets/
mkdir -p "$ROOT/Assets/Icons" "$ROOT/Assets/Images"
if [ -d Assets ]; then
    cp -a Assets/. "$ROOT/Assets/"
fi
# 商店权威源：ToyImage/Store → Guest Store/（store-src；废 Assets/Store + StoreCache）
# CHAT-4：chat 仅留在 ToyImage/Store 供 export-store-lan；Guest 须 LAN install。
if [ -d Store ]; then
    mkdir -p "$ROOT/Store"
    cp -a Store/. "$ROOT/Store/"
    rm -rf "$ROOT/Store/packages/chat" "$ROOT/Apps/chat"
    if [ -f "$ROOT/Store/catalog.txt" ]; then
        grep -v '^chat|' "$ROOT/Store/catalog.txt" > "$ROOT/Store/catalog.txt.noch" \
            && mv "$ROOT/Store/catalog.txt.noch" "$ROOT/Store/catalog.txt"
    fi
fi
rm -rf "$ROOT/Assets/Store" "$ROOT/StoreCache"
# 无线固件（PR-N-wifi-1）：须随 TOYOS 进 U 盘；缺则 iwl fw=miss
if [ ! -f "$ROOT/FW/IWL8265.UCODE" ]; then
    echo "warning: $ROOT/FW/IWL8265.UCODE missing — sync-usb will warn; NUC iwl needs it" >&2
elif [ "${TOY_QEMU_VERBOSE:-0}" = 1 ]; then
    echo "FW/IWL8265.UCODE present ($(stat -c%s "$ROOT/FW/IWL8265.UCODE") bytes)"
fi
mkdir -p "$ROOT/Assets/Packs"
if [ -d Assets/Packs ]; then
    cp -a Assets/Packs/. "$ROOT/Assets/Packs/" 2>/dev/null || true
fi

# Guest 可写占位；已装 Apps 与根目录 ELF 对齐（防旧号段残留）
mkdir -p "$ROOT/Apps" "$ROOT/Store"
if [ -f "$ROOT/HELLO.ELF" ]; then
    mkdir -p "$ROOT/Apps/hello"
    cp -f "$ROOT/HELLO.ELF" "$ROOT/Apps/hello/HELLO.ELF"
fi
if [ -f "$ROOT/GUIDEMO.ELF" ]; then
    mkdir -p "$ROOT/Apps/guidemo"
    cp -f "$ROOT/GUIDEMO.ELF" "$ROOT/Apps/guidemo/GUIDEMO.ELF"
fi

printf "ToyOS root volume\n" > "$ROOT/TOYOS.ID"
if [ "${TOY_QEMU_VERBOSE:-0}" = 1 ]; then
    echo "Prepared $ROOT (TOYOS system disk):"
    ls -lh "$ROOT"
    find "$ROOT/Assets" -type f 2>/dev/null | sort || true
else
    echo "Prepared $ROOT (TOYOS system disk)"
fi
