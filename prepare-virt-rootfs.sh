#!/bin/bash
# 准备 virt 系统盘目录（FAT via QEMU fat:rw），供 run-virt-*.sh 挂 virtio-blk
# 优先复用 ../ToyImage/rootfs；否则写最小 TOYOS.ID + THEME.CFG
set -e
cd "$(dirname "$0")"
ROOT=virt-rootfs
mkdir -p "$ROOT"

IMG_ROOT="../ToyImage/rootfs"
if [ -d "$IMG_ROOT" ]; then
    for F in TOYOS.ID THEME.CFG HELLO.ELF CAT.ELF WRITE.ELF \
             SYSHELLO.ELF EXECDEMO.ELF PIPEDEMO.ELF BRKDEMO.ELF; do
        if [ -f "$IMG_ROOT/$F" ]; then
            cp -f "$IMG_ROOT/$F" "$ROOT/$F"
        fi
    done
fi

if [ ! -f "$ROOT/TOYOS.ID" ]; then
    printf "ToyOS root volume\n" > "$ROOT/TOYOS.ID"
fi
if [ ! -f "$ROOT/THEME.CFG" ]; then
    cat > "$ROOT/THEME.CFG" <<'EOF'
mode=800x600
desktop_bg=0x203040
EOF
fi

echo "Prepared $ROOT:"
ls -la "$ROOT"
