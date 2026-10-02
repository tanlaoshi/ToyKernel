#!/bin/bash
# sync-nuc.sh — 同步到 NUC SSD 上的 ToyOS 分区（日常路径）
#
# 布局：
#   LABEL=ToyOS（nvme 数据分区）→ Kernel.elf / RootFs / FW/ …
#   /boot/efi/EFI/toyos/BOOTX64.EFI ← 仅 --boot 时更新（grub chainloader）
#
# U 盘双分区仍用 Scripts/sync-usb.sh（备选）。
#
# 用法：
#   ./sync-nuc.sh                 # RootFs → ToyOS（不动 Boot）
#   ./sync-nuc.sh --kernel-only   # 只更新 Kernel.elf + FW/
#   ./sync-nuc.sh --boot          # 另写 EFI/toyos/BOOTX64.EFI（需可写 ESP）
#   ./sync-nuc.sh --build         # 先编 Kernel（+ Boot 若 --boot），再 prepare-rootfs，再同步
#   TOY_TOYOS_MNT=/mnt/toy ./sync-nuc.sh
#   TOY_ESP_BOOT_DIR=/boot/efi/EFI/toyos ./sync-nuc.sh --boot
#
# WIFI.CFG / WIFI_H.CFG / WIFI_C.CFG：保留在 ToyOS 上，不同步覆盖。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
ROOT="$(toyos_image_root "$0")"
DO_BUILD=0
KERNEL_ONLY=0
DO_BOOT=0
ESP_BOOT_DIR="${TOY_ESP_BOOT_DIR:-/boot/efi/EFI/toyos}"

sync_toyos_fw() {
  local Dest="$1"
  local SrcFw="$ROOT/RootFs/X64/FW"
  if [[ ! -d "$SrcFw" ]]; then
    echo "warning: missing $SrcFw — iwl8265 will fw=miss until FW/ is present" >&2
    return 0
  fi
  mkdir -p "$Dest/FW"
  if command -v rsync >/dev/null 2>&1; then
    rsync -rltD --delete \
      --exclude 'WIFI.CFG' \
      --exclude 'WIFI_H.CFG' \
      --exclude 'WIFI_C.CFG' \
      --no-owner --no-group --no-perms \
      "$SrcFw/" "$Dest/FW/"
  else
    local f base
    for f in "$SrcFw"/*; do
      [[ -e "$f" ]] || continue
      base="$(basename "$f")"
      if [[ "$base" == "WIFI.CFG" || "$base" == "WIFI_H.CFG" || "$base" == "WIFI_C.CFG" ]]; then
        [[ -f "$Dest/FW/$base" ]] && continue
      fi
      cp -f "$f" "$Dest/FW/"
    done
  fi
  if [[ -f "$Dest/FW/IWL8265.UCODE" ]]; then
    echo "FW/IWL8265.UCODE -> $Dest/FW/ ($(stat -c%s "$Dest/FW/IWL8265.UCODE") bytes)"
  else
    echo "warning: $Dest/FW/ has no IWL8265.UCODE" >&2
  fi
  if [[ -f "$Dest/FW/WIFI.CFG" ]]; then
    echo "FW/WIFI.CFG kept on ToyOS (not overwritten)"
  else
    echo "note: no FW/WIFI.CFG — cp WIFI_H.CFG or WIFI_C.CFG → WIFI.CFG"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    --build|-b) DO_BUILD=1; shift ;;
    --kernel-only|-k) KERNEL_ONLY=1; shift ;;
    --boot) DO_BOOT=1; shift ;;
    *)
      echo "unknown arg: $1 (USB 备选请用 sync-usb.sh)" >&2
      exit 1
      ;;
  esac
done

find_label_dev() {
  blkid -L "$1" 2>/dev/null || true
}

find_toyos_dev() {
  local D Lab
  for Lab in ToyOS TOYOS; do
    D="$(find_label_dev "$Lab")"
    if [[ -n "$D" ]]; then
      printf '%s\n' "$D"
      return 0
    fi
  done
  return 1
}

find_dev_mnt() {
  local Mnt
  Mnt="$(lsblk -ln -o MOUNTPOINT "$1" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Mnt" || "$Mnt" == "-" ]]; then
    return 1
  fi
  printf '%s\n' "$Mnt"
}

ensure_toyos_mounted() {
  local Dev="$1"
  local Mnt Out
  Mnt="$(find_dev_mnt "$Dev" || true)"
  if [[ -n "$Mnt" ]]; then
    printf '%s\n' "$Mnt"
    return 0
  fi
  if command -v udisksctl >/dev/null 2>&1; then
    Out="$(udisksctl mount -b "$Dev" 2>/dev/null || true)"
    if [[ "$Out" =~ Mounted\ .*\ at\ (.+)$ ]]; then
      Mnt="${BASH_REMATCH[1]}"
      echo "mounted $Dev -> $Mnt (udisksctl)" >&2
      printf '%s\n' "$Mnt"
      return 0
    fi
  fi
  echo "error: ToyOS at $Dev not mounted. Try: udisksctl mount -b $Dev" >&2
  return 1
}

pick_kernel_src() {
  local Bld Rfs
  Bld="$(toyos_kernel_build_dir "$(toyos_resolve_root "$0")")/HAL/X64/Kernel.elf"
  Rfs="$ROOT/RootFs/X64/Kernel.elf"
  if [[ -f "$Bld" && -f "$Rfs" ]]; then
    if [[ "$Bld" -nt "$Rfs" ]]; then
      printf '%s\n' "$Bld"
    else
      printf '%s\n' "$Rfs"
    fi
  elif [[ -f "$Bld" ]]; then
    printf '%s\n' "$Bld"
  elif [[ -f "$Rfs" ]]; then
    printf '%s\n' "$Rfs"
  else
    return 1
  fi
}

sync_boot_chainloader() {
  local BootEfi="$1"
  local DestDir="$ESP_BOOT_DIR"
  local Dest="$DestDir/BOOTX64.EFI"
  if [[ ! -f "$BootEfi" ]]; then
    echo "error: missing $BootEfi — build ToyBoot first" >&2
    return 1
  fi
  if [[ -d "$DestDir" && -w "$DestDir" ]]; then
    cp -f "$BootEfi" "$Dest"
  elif command -v sudo >/dev/null 2>&1; then
    sudo mkdir -p "$DestDir"
    sudo cp -f "$BootEfi" "$Dest"
  else
    echo "error: cannot write $Dest (ESP 需 root)" >&2
    return 1
  fi
  sync
  local Sz
  Sz="$(stat -c%s "$Dest" 2>/dev/null || sudo stat -c%s "$Dest")"
  echo "Boot -> $Dest ($Sz bytes)"
}

TOY_MNT="${TOY_TOYOS_MNT:-}"
TOY_DEV=""

if [[ -z "$TOY_MNT" ]]; then
  TOY_DEV="$(find_toyos_dev || true)"
  if [[ -n "$TOY_DEV" ]]; then
    TOY_MNT="$(find_dev_mnt "$TOY_DEV" || true)"
  fi
fi
if [[ -z "$TOY_MNT" ]]; then
  for d in /media/*/ToyOS /media/*/TOYOS \
           /media/"${USER:-tank}"/ToyOS /media/"${USER:-tank}"/TOYOS \
           /run/media/"${USER:-tank}"/ToyOS /run/media/"${USER:-tank}"/TOYOS; do
    if [[ -d "$d" ]]; then TOY_MNT="$d"; break; fi
  done
fi
if [[ -z "$TOY_MNT" ]]; then
  if [[ -z "$TOY_DEV" ]]; then
    TOY_DEV="$(find_toyos_dev || true)"
  fi
  if [[ -n "$TOY_DEV" ]]; then
    TOY_MNT="$(ensure_toyos_mounted "$TOY_DEV" || true)"
  fi
fi
if [[ -z "$TOY_MNT" ]]; then
  echo "error: ToyOS volume not found/mounted." >&2
  echo "  Expected LABEL=ToyOS on SSD. USB 备选: $SCRIPT_DIR/sync-usb.sh" >&2
  exit 1
fi
if [[ ! -d "$TOY_MNT" || ! -w "$TOY_MNT" ]]; then
  echo "error: ToyOS not writable: $TOY_MNT" >&2
  exit 1
fi

BOOT_EFI="$ROOT/Esp/X64/EFI/BOOT/BOOTX64.EFI"

if [[ "$KERNEL_ONLY" -eq 1 ]]; then
  if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "== build Kernel =="
    (cd "$ROOT/../ToyKernel" && ./build.sh)
  fi
  SRC="$(pick_kernel_src || true)"
  if [[ -z "$SRC" ]]; then
    echo "error: no Kernel.elf — ./build.sh 或去掉 --kernel-only" >&2
    exit 1
  fi
  cp -f "$SRC" "$TOY_MNT/Kernel.elf"
  sync_toyos_fw "$TOY_MNT"
  if [[ ! -f "$TOY_MNT/TOYOS.ID" ]]; then
    printf "ToyOS root volume\n" > "$TOY_MNT/TOYOS.ID"
  fi
  if [[ "$DO_BOOT" -eq 1 ]]; then
    if [[ "$DO_BUILD" -eq 1 && -x "$ROOT/../ToyBoot/build.sh" ]]; then
      (cd "$ROOT/../ToyBoot" && ./build.sh)
    fi
    sync_boot_chainloader "$BOOT_EFI"
  fi
  sync
  echo "=== nuc kernel-only ==="
  echo "ToyOS -> $TOY_MNT"
  echo "src   -> $SRC"
  ls -l --time-style=long-iso "$SRC" "$TOY_MNT/Kernel.elf" "$TOY_MNT/TOYOS.ID"
  ls -l --time-style=long-iso "$TOY_MNT/FW/IWL8265.UCODE" 2>/dev/null || true
  ls -l --time-style=long-iso "$TOY_MNT/FW/WIFI.CFG" 2>/dev/null || true
  md5sum "$SRC" "$TOY_MNT/Kernel.elf"
  exit 0
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
  echo "== build Kernel =="
  (cd "$ROOT/../ToyKernel" && ./build.sh)
  if [[ "$DO_BOOT" -eq 1 ]]; then
    echo "== build Boot =="
    if [[ -x "$ROOT/../ToyBoot/build.sh" ]]; then
      (cd "$ROOT/../ToyBoot" && ./build.sh)
    else
      echo "warning: ToyBoot/build.sh missing" >&2
    fi
  fi
fi

echo "== prepare-rootfs =="
"$SCRIPT_DIR/prepare-rootfs.sh"

echo "ToyOS -> $TOY_MNT"
if [[ "$DO_BOOT" -eq 1 ]]; then
  echo "== sync chainloader Boot =="
  sync_boot_chainloader "$BOOT_EFI"
fi

echo "== sync ToyOS (RootFs/X64) =="
if command -v rsync >/dev/null 2>&1; then
  rsync -rltD --delete \
    --no-owner --no-group --no-perms \
    --exclude 'System Volume Information' \
    --exclude '.Trash*' \
    --exclude 'lost+found' \
    --exclude 'FW/WIFI.CFG' \
    --exclude 'FW/WIFI_H.CFG' \
    --exclude 'FW/WIFI_C.CFG' \
    "$ROOT/RootFs/X64/" "$TOY_MNT/"
else
  cp -a "$ROOT/RootFs/X64/." "$TOY_MNT/"
fi
sync_toyos_fw "$TOY_MNT"

# 双分区：Boot 只在 /boot/efi/EFI/toyos；清掉误写到 ToyOS 的 EFI/
if [[ -d "$TOY_MNT/EFI" ]]; then
  echo "note: removing stale $TOY_MNT/EFI (NUC Boot is on ESP EFI/toyos only)"
  rm -rf "$TOY_MNT/EFI"
fi

if [[ ! -f "$TOY_MNT/TOYOS.ID" ]]; then
  printf "ToyOS root volume\n" > "$TOY_MNT/TOYOS.ID"
fi

sync
echo
echo "=== nuc sync done ==="
[[ "$DO_BOOT" -eq 1 ]] && echo "Boot : $ESP_BOOT_DIR/BOOTX64.EFI"
echo "Kernel : $(stat -c%s "$TOY_MNT/Kernel.elf") bytes @ $TOY_MNT"
ls -lh "$TOY_MNT/Kernel.elf" "$TOY_MNT/TOYOS.ID" 2>/dev/null || true
echo "日常只跑本脚本；U 盘备选 sync-usb.sh。ToyBoot 有改动再加 --boot。"
