#!/bin/bash
# make-usb-stick.sh — 把一块 USB 盘做成 ToyOS 真机启动盘（GPT）
#
# 布局（与家里一致）：
#   p1 ESP   256MiB  FAT32  LABEL=ESP   → EFI/BOOT/BOOTX64.EFI
#   p2 TOYOS 剩余    FAT32  LABEL=TOYOS → Kernel.elf / TOYOS.ID / Assets / *.ELF
#
# 用法：
#   ./make-usb-stick.sh                  # 自动找唯一 USB 盘，仅打印将做什么
#   ./make-usb-stick.sh --device /dev/sdX --yes
#   ./make-usb-stick.sh --yes            # 唯一 USB 时直接分区+格式化
#   ./make-usb-stick.sh --yes --sync     # 格式化后立刻 ./sync-usb.sh
#
# 危险：会清空整盘。仅允许 TRAN=usb 的块设备。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/toyos-common.sh"
ROOT="$SCRIPT_DIR"
IMAGE_ROOT="$(toyos_image_root "$0")"
DEVICE=""
DO_YES=0
DO_SYNC=0
ESP_MIB=256

# 分区/mkfs 需要 root；非 root 时自动提权（会提示密码）
if [[ "$(id -u)" -ne 0 ]]; then
  echo "make-usb-stick: elevating with sudo (needs disk wipe rights)..."
  exec sudo --preserve-env=TOY_ESP_MNT,TOY_TOYOS_MNT "$0" "$@"
fi

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \?//'
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage 0 ;;
    --device|-d)
      DEVICE="${2:-}"
      shift 2
      ;;
    --yes|-y) DO_YES=1; shift ;;
    --sync) DO_SYNC=1; shift ;;
    --esp-mib)
      ESP_MIB="${2:-}"
      shift 2
      ;;
    *)
      echo "unknown arg: $1" >&2
      usage 1
      ;;
  esac
done

list_usb_disks() {
  lsblk -dn -o NAME,TRAN,SIZE,MODEL,TYPE | awk '$2=="usb" && $5=="disk" {print "/dev/"$1, $3, $4}'
}

is_usb_disk() {
  local Dev="$1"
  local Name Tran Type
  Name="$(basename "$Dev")"
  Tran="$(lsblk -dn -o TRAN "/dev/$Name" 2>/dev/null || true)"
  Type="$(lsblk -dn -o TYPE "/dev/$Name" 2>/dev/null || true)"
  [[ "$Type" == "disk" && "$Tran" == "usb" ]]
}

if [[ -z "$DEVICE" ]]; then
  mapfile -t USBS < <(list_usb_disks)
  if [[ ${#USBS[@]} -eq 0 ]]; then
    echo "error: no USB disk found (lsblk TRAN=usb)" >&2
    exit 1
  fi
  if [[ ${#USBS[@]} -gt 1 ]]; then
    echo "error: multiple USB disks; pass --device explicitly:" >&2
    printf '  %s\n' "${USBS[@]}" >&2
    exit 1
  fi
  DEVICE="${USBS[0]%% *}"
fi

DEVICE="$(readlink -f "$DEVICE")"
if [[ ! -b "$DEVICE" ]]; then
  echo "error: not a block device: $DEVICE" >&2
  exit 1
fi
if ! is_usb_disk "$DEVICE"; then
  echo "error: $DEVICE is not a USB disk (refuse non-usb)" >&2
  lsblk -o NAME,TRAN,SIZE,MODEL,TYPE "$DEVICE" >&2 || true
  exit 1
fi

SIZE_BYTES="$(blockdev --getsize64 "$DEVICE")"
# 拒绝看起来像内置大盘的误选（USB 一般 < 2TiB 且 TRAN 已过滤）
if [[ "$SIZE_BYTES" -lt $((512 * 1024 * 1024)) ]]; then
  echo "error: $DEVICE too small (<512MiB)" >&2
  exit 1
fi

echo "=== ToyOS USB stick plan ==="
echo "device : $DEVICE"
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT,TRAN,MODEL "$DEVICE"
echo "layout : GPT | ESP ${ESP_MIB}MiB (FAT32/ESP) | TOYOS rest (FAT32/TOYOS)"
echo

if [[ "$DO_YES" -ne 1 ]]; then
  echo "dry-run only. Re-run with --yes to WIPE and repartition."
  exit 0
fi

echo "WARNING: wiping all partitions on $DEVICE in 3s..."
sleep 3

# 卸下该盘上所有挂载
while read -r Mnt; do
  [[ -n "$Mnt" ]] || continue
  echo "umount $Mnt"
  umount "$Mnt" 2>/dev/null || umount -l "$Mnt" 2>/dev/null || true
done < <(lsblk -ln -o MOUNTPOINT "$DEVICE" | awk 'NF')

# GPT + 两分区
sgdisk --zap-all "$DEVICE"
sgdisk -n "1:1MiB:+${ESP_MIB}MiB" -t 1:EF00 -c 1:"ESP" "$DEVICE"
sgdisk -n "2:0:0" -t 2:0700 -c 2:"TOYOS" "$DEVICE"
sgdisk -p "$DEVICE"
partprobe "$DEVICE" 2>/dev/null || true
sleep 1

# 分区节点名：/dev/sda → sda1/sda2；NVMe USB 少见 → p1/p2
P1="${DEVICE}1"
P2="${DEVICE}2"
if [[ ! -b "$P1" && -b "${DEVICE}p1" ]]; then
  P1="${DEVICE}p1"
  P2="${DEVICE}p2"
fi
if [[ ! -b "$P1" || ! -b "$P2" ]]; then
  echo "error: partitions not found after sgdisk ($P1 $P2)" >&2
  lsblk "$DEVICE" >&2
  exit 1
fi

mkfs.vfat -F 32 -n ESP "$P1"
mkfs.vfat -F 32 -n TOYOS "$P2"

echo "formatted:"
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE"

# 尝试挂载（桌面自动挂载或手动）；FAT 用调用者 uid 以便后续 sync-usb 免 root
ESP_MNT="/mnt/toyos-esp"
TOY_MNT="/mnt/toyos-data"
MUID="${SUDO_UID:-0}"
MGID="${SUDO_GID:-0}"
mkdir -p "$ESP_MNT" "$TOY_MNT"
mount -o "uid=${MUID},gid=${MGID},umask=022" "$P1" "$ESP_MNT"
mount -o "uid=${MUID},gid=${MGID},umask=022" "$P2" "$TOY_MNT"
echo "mounted ESP=$ESP_MNT TOYOS=$TOY_MNT (uid=${MUID})"

if [[ "$DO_SYNC" -eq 1 ]]; then
  export TOY_ESP_MNT="$ESP_MNT"
  export TOY_TOYOS_MNT="$TOY_MNT"
  "$ROOT/sync-usb.sh"
else
  echo
  echo "Next: TOY_ESP_MNT=$ESP_MNT TOY_TOYOS_MNT=$TOY_MNT $ROOT/sync-usb.sh"
  echo "Or unplug/replug and let the desktop mount ESP+TOYOS, then ./sync-usb.sh"
fi
