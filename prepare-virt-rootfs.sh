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
             SYSHELLO.ELF EXECDEMO.ELF PIPEDEMO.ELF BRKDEMO.ELF KILLDEMO.ELF WINDEMO.ELF GUIDEMO.ELF TOYOS.DB; do
        if [ -f "$IMG_ROOT/$F" ]; then
            cp -f "$IMG_ROOT/$F" "$ROOT/$F"
        fi
    done
    # 运行时资源树（不链入内核）
    if [ -d "$IMG_ROOT/Assets" ]; then
        mkdir -p "$ROOT/Assets"
        cp -a "$IMG_ROOT/Assets/." "$ROOT/Assets/"
    fi
    # 兼容旧扁平 WALL.BMP
    if [ -f "$IMG_ROOT/WALL.BMP" ]; then
        mkdir -p "$ROOT/Assets/Images"
        cp -f "$IMG_ROOT/WALL.BMP" "$ROOT/Assets/Images/WALL.BMP"
    fi
fi
# PR-G13：壁纸样本（Image 未同步时用仓库 Assets/）
if [ ! -f "$ROOT/Assets/Images/WALL.BMP" ] && [ -f Assets/Images/WALL.BMP ]; then
    mkdir -p "$ROOT/Assets/Images"
    cp -f Assets/Images/WALL.BMP "$ROOT/Assets/Images/WALL.BMP"
fi
# 桌面图标（Lucide → bmp48；Image 未带 Icons 时用仓库）
if [ ! -f "$ROOT/Assets/Icons/bmp48/SHELL.BMP" ] && [ -d Assets/Icons ]; then
    mkdir -p "$ROOT/Assets/Icons"
    cp -a Assets/Icons/. "$ROOT/Assets/Icons/"
fi
if [ ! -f "$ROOT/Assets/Locale/en.txt" ] && [ -d Assets/Locale ]; then
    mkdir -p "$ROOT/Assets/Locale"
    cp -a Assets/Locale/. "$ROOT/Assets/Locale/"
fi
if [ ! -f "$ROOT/Assets/Fonts/VGA8X16.FNT" ] && [ -d Assets/Fonts ]; then
    mkdir -p "$ROOT/Assets/Fonts"
    cp -a Assets/Fonts/. "$ROOT/Assets/Fonts/"
fi
if [ ! -f "$ROOT/Assets/Store/catalog.txt" ] && [ -d Assets/Store ]; then
    mkdir -p "$ROOT/Assets/Store"
    cp -a Assets/Store/. "$ROOT/Assets/Store/"
fi
mkdir -p "$ROOT/Assets/Icons" "$ROOT/Assets/Locale" "$ROOT/Assets/Fonts" "$ROOT/Assets/Store"
mkdir -p "$ROOT/Apps" "$ROOT/Store"
if [ -d Apps ]; then
    cp -a Apps/. "$ROOT/Apps/" 2>/dev/null || true
fi
if [ -d Store ]; then
    cp -a Store/. "$ROOT/Store/" 2>/dev/null || true
fi
# 清理旧扁平落点，避免双份
rm -f "$ROOT/WALL.BMP"

# PR-A12：本 arch 用户 HELLO 覆盖盘上的 x86 机型
ARCH="${TOY_VIRT_MAKE_ARCH:-}"
HAL_ARCH="${TOY_VIRT_HAL_ARCH:-}"
if [ -z "$HAL_ARCH" ]; then
    case "$ARCH" in
        arm64) HAL_ARCH=Arm64 ;;
        riscv) HAL_ARCH=RiscV ;;
    esac
fi
if [ -n "$HAL_ARCH" ] && [ -f "Build/HAL/$HAL_ARCH/user/hello.elf" ]; then
    cp -f "Build/HAL/$HAL_ARCH/user/hello.elf" "$ROOT/HELLO.ELF"
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

    def _read_dir(self, dir_cl):
        """dir_cl=0 → FAT16 root; else directory cluster chain start."""
        if dir_cl == 0:
            self.f.seek(self.root_off)
            return bytearray(self.f.read(self.root_size)), 0
        clsz = self.spc * self.bps
        data = bytearray()
        cl = dir_cl
        seen = set()
        while cl >= 2 and cl < 0xFFF8:
            if cl in seen:
                break
            seen.add(cl)
            self.f.seek(self.cluster_off(cl))
            data.extend(self.f.read(clsz))
            cl = self.fat_get(cl)
        return data, dir_cl

    def _write_dir(self, dir_cl, data):
        if dir_cl == 0:
            if len(data) > self.root_size:
                raise RuntimeError("root full")
            buf = bytearray(self.root_size)
            buf[: len(data)] = data
            self.f.seek(self.root_off)
            self.f.write(buf)
            return
        clsz = self.spc * self.bps
        # grow chain if needed
        need = (len(data) + clsz - 1) // clsz
        clusters = []
        cl = dir_cl
        while cl >= 2 and cl < 0xFFF8:
            clusters.append(cl)
            nxt = self.fat_get(cl)
            if nxt >= 0xFFF8:
                break
            cl = nxt
        while len(clusters) < need:
            ncl = self.alloc_cluster()
            self.fat_set(clusters[-1], ncl)
            clusters.append(ncl)
        self.fat_set(clusters[-1], 0xFFFF)
        pad = bytearray(need * clsz)
        pad[: len(data)] = data
        for i, c in enumerate(clusters[:need]):
            self.f.seek(self.cluster_off(c))
            self.f.write(pad[i * clsz : (i + 1) * clsz])

    def _find_slot(self, data):
        for i in range(0, len(data), 32):
            c = data[i]
            if c == 0 or c == 0xE5:
                return i
        # append (extend directory)
        data.extend(b"\x00" * 32)
        return len(data) - 32

    def _name83(self, name):
        p = pathlib.Path(name)
        stem = p.stem.upper()[:8]
        ext = p.suffix.upper()[1:4]
        return stem.ljust(8) + (ext.ljust(3) if ext else "   ")

    def _lookup(self, dir_cl, name83):
        data, _ = self._read_dir(dir_cl)
        target = name83.encode("ascii")
        for i in range(0, len(data), 32):
            if data[i] in (0, 0xE5):
                continue
            if data[i + 11] == 0x0F:
                continue
            if data[i : i + 11] == target:
                cl = struct.unpack_from("<H", data, i + 26)[0]
                attr = data[i + 11]
                return cl, attr
        return None, None

    def add_dir(self, parent_cl, name):
        name83 = self._name83(name)
        existing, attr = self._lookup(parent_cl, name83)
        if existing is not None:
            if not (attr & 0x10):
                raise RuntimeError(f"{name} exists as file")
            return existing
        cl = self.alloc_cluster()
        clsz = self.spc * self.bps
        # empty dir with . and ..
        ent = bytearray(clsz)
        # .
        ent[0:11] = b".          "
        ent[11] = 0x10
        struct.pack_into("<H", ent, 26, cl)
        # ..
        ent[32:43] = b"..         "
        ent[43] = 0x10
        struct.pack_into("<H", ent, 32 + 26, parent_cl)
        self.f.seek(self.cluster_off(cl))
        self.f.write(ent)

        data, _ = self._read_dir(parent_cl)
        slot = self._find_slot(data)
        if slot + 32 > len(data):
            data.extend(b"\x00" * 32)
        e = bytearray(32)
        e[0:11] = name83.encode("ascii")
        e[11] = 0x10
        struct.pack_into("<H", e, 26, cl)
        data[slot : slot + 32] = e
        # trim trailing zeros but keep at least through slot
        end = max(slot + 32, next((i for i in range(len(data) - 1, -1, -1) if data[i]), 0) + 1)
        end = (end + 31) // 32 * 32
        self._write_dir(parent_cl, data[:end])
        return cl

    def add_file(self, parent_cl, name, data_bytes: bytes):
        name83 = self._name83(name)
        clusters = []
        remain = data_bytes
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

        dir_data, _ = self._read_dir(parent_cl)
        # replace existing file entry if present
        target = name83.encode("ascii")
        slot = None
        for i in range(0, len(dir_data), 32):
            if dir_data[i] in (0, 0xE5):
                if slot is None:
                    slot = i
                continue
            if dir_data[i + 11] == 0x0F:
                continue
            if dir_data[i : i + 11] == target:
                slot = i
                break
        if slot is None:
            slot = self._find_slot(dir_data)
            if slot + 32 > len(dir_data):
                dir_data.extend(b"\x00" * 32)
        e = bytearray(32)
        e[0:11] = target
        e[11] = 0x20
        struct.pack_into("<H", e, 26, first)
        struct.pack_into("<I", e, 28, len(data_bytes))
        dir_data[slot : slot + 32] = e
        end = max(slot + 32, next((i for i in range(len(dir_data) - 1, -1, -1) if dir_data[i]), 0) + 1)
        end = (end + 31) // 32 * 32
        self._write_dir(parent_cl, dir_data[:end])

    def close(self):
        self.write_fat()
        self.f.close()

def pack_tree(fat: Fat16, host: pathlib.Path, parent_cl: int):
    for p in sorted(host.iterdir()):
        if p.name.startswith("."):
            continue
        if p.is_dir():
            cl = fat.add_dir(parent_cl, p.name)
            pack_tree(fat, p, cl)
        elif p.is_file():
            fat.add_file(parent_cl, p.name, p.read_bytes())

fat = Fat16(img)
pack_tree(fat, src, 0)
fat.close()
print(f"Packed {img} from {src}/")
PY

echo "Prepared $ROOT + $IMG:"
ls -la "$ROOT" "$IMG"
find "$ROOT/Assets" -type f 2>/dev/null | sort || true
