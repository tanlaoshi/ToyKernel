#!/bin/bash
# 准备 virt 系统盘：同步文件到 virt-rootfs/，并打成 raw FAT16 镜像 virt-rootfs.img
# （QEMU fat:rw/vvfat 与 virtio-net 同机时会破坏 TX；N10 改用真 FAT 镜像）
set -e
cd "$(dirname "$0")"
ROOT=virt-rootfs
IMG=virt-rootfs.img
mkdir -p "$ROOT"

IMG_ROOT="../ToyImage/rootfs"
if [ -d "$IMG_ROOT" ]; then
    for F in TOYOS.ID THEME.CFG HELLO.ELF CAT.ELF WRITE.ELF \
             SYSHELLO.ELF EXECDEMO.ELF PIPEDEMO.ELF BRKDEMO.ELF KILLDEMO.ELF TOYOS.DB; do
        if [ -f "$IMG_ROOT/$F" ]; then
            cp -f "$IMG_ROOT/$F" "$ROOT/$F"
        fi
    done
fi

# PR-A12：本 arch 用户 HELLO 覆盖盘上的 x86 机型
ARCH="${TOY_VIRT_MAKE_ARCH:-}"
if [ -n "$ARCH" ] && [ -f "Build/$ARCH/user/hello.elf" ]; then
    cp -f "Build/$ARCH/user/hello.elf" "$ROOT/HELLO.ELF"
elif [ -f virt-rootfs/HELLO.ELF ] && [ -n "$ARCH" ]; then
    : # already staged by build.sh
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

python3 - "$ROOT" "$IMG" <<'PY'
import os, struct, subprocess, pathlib, sys

src = pathlib.Path(sys.argv[1])
img = pathlib.Path(sys.argv[2])
size = 16 * 1024 * 1024

with open(img, "wb") as f:
    f.truncate(size)
subprocess.check_call(
    ["mkfs.fat", "-F", "16", "-n", "TOYOS", str(img)],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)

class Fat16:
    def __init__(self, path):
        self.f = open(path, "r+b")
        b = self.f.read(512)
        self.bps = struct.unpack_from("<H", b, 11)[0]
        self.spc = b[13]
        self.rsv = struct.unpack_from("<H", b, 14)[0]
        self.nfats = b[16]
        self.root_ents = struct.unpack_from("<H", b, 17)[0]
        self.spf = struct.unpack_from("<H", b, 22)[0]
        self.fat_off = self.rsv * self.bps
        self.root_off = self.fat_off + self.nfats * self.spf * self.bps
        self.root_size = self.root_ents * 32
        self.data_off = self.root_off + self.root_size
        self.f.seek(self.fat_off)
        self.fat = bytearray(self.f.read(self.spf * self.bps))

    def fat_get(self, cl):
        return struct.unpack_from("<H", self.fat, cl * 2)[0]

    def fat_set(self, cl, val):
        struct.pack_into("<H", self.fat, cl * 2, val)

    def alloc_cluster(self):
        for cl in range(2, len(self.fat) // 2):
            if self.fat_get(cl) == 0:
                self.fat_set(cl, 0xFFFF)
                return cl
        raise RuntimeError("FAT full")

    def write_fat(self):
        for i in range(self.nfats):
            off = self.fat_off + i * self.spf * self.bps
            self.f.seek(off)
            self.f.write(self.fat)

    def cluster_off(self, cl):
        return self.data_off + (cl - 2) * self.spc * self.bps

    def add_file(self, name83, data: bytes):
        self.f.seek(self.root_off)
        root = bytearray(self.f.read(self.root_size))
        slot = None
        for i in range(0, len(root), 32):
            c = root[i]
            if c == 0 or c == 0xE5:
                slot = i
                break
        if slot is None:
            raise RuntimeError("root full")
        clusters = []
        remain = data
        clsz = self.spc * self.bps
        if not remain:
            first = 0
        else:
            while True:
                cl = self.alloc_cluster()
                clusters.append(cl)
                chunk = remain[:clsz]
                remain = remain[clsz:]
                self.f.seek(self.cluster_off(cl))
                self.f.write(chunk)
                if len(chunk) < clsz:
                    self.f.write(b"\x00" * (clsz - len(chunk)))
                if not remain:
                    break
            for a, b_ in zip(clusters, clusters[1:]):
                self.fat_set(a, b_)
            self.fat_set(clusters[-1], 0xFFFF)
            first = clusters[0]
        if "." in name83:
            name, ext = name83.upper().split(".", 1)
        else:
            name, ext = name83.upper(), ""
        name = name[:8].ljust(8)
        ext = ext[:3].ljust(3)
        ent = bytearray(32)
        ent[0:8] = name.encode("ascii")
        ent[8:11] = ext.encode("ascii")
        ent[11] = 0x20
        struct.pack_into("<H", ent, 26, first)
        struct.pack_into("<I", ent, 28, len(data))
        root[slot : slot + 32] = ent
        self.f.seek(self.root_off)
        self.f.write(root)

    def close(self):
        self.write_fat()
        self.f.close()

def to83(path: pathlib.Path) -> str:
    stem = path.stem.upper()[:8]
    ext = path.suffix.upper()[1:4]
    return f"{stem}.{ext}" if ext else stem

fat = Fat16(img)
for p in sorted(src.iterdir()):
    if p.is_file() and not p.name.startswith("."):
        fat.add_file(to83(p), p.read_bytes())
fat.close()
print(f"Packed {img} from {src}/")
PY

echo "Prepared $ROOT + $IMG:"
ls -la "$ROOT" "$IMG"
