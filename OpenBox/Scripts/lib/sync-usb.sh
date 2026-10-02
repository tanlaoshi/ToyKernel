#!/bin/bash
# sync-usb.sh — 把当前 ToyImage / 构建产物同步到已挂载的 ESP + TOYOS 分区
#
# 真机布局（见 make-usb-stick.sh）：
#   ESP   → EFI/BOOT/BOOTX64.EFI
#   TOYOS → RootFs/X64/（Kernel.elf、TOYOS.ID、THEME、Assets、FW/、*.ELF…）
#
# 用法：
#   ./sync-usb.sh                 # 自动找 LABEL=ESP 与 LABEL=TOYOS
#   ./sync-usb.sh --build         # 先编 Kernel + Boot，再 prepare-rootfs，再同步
#   ./sync-usb.sh --kernel-only   # 只更新 TOYOS/Kernel.elf + FW/（快迭代；含 iwl 固件）
#   TOY_ESP_MNT=/mnt/esp TOY_TOYOS_MNT=/mnt/toy ./sync-usb.sh
#
# 兼容旧单分区：若只有 TOYOS、没有 ESP，则把 EFI 也写进 TOYOS（单 FAT 布局）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=toyos-common.sh
. "$SCRIPT_DIR/toyos-common.sh"
ROOT="$(toyos_image_root "$0")"
DO_BUILD=0
KERNEL_ONLY=0

# TOYOS 卷上的无线固件目录（PR-N-wifi-1：FW/IWL8265.UCODE）
# WIFI.CFG / WIFI_H.CFG / WIFI_C.CFG 含 PSK：只活在 U 盘上；同步时保留、不覆盖、不 --delete。
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
    # 无 rsync：逐文件拷；已有 WIFI*.CFG 不覆盖
    local f
    for f in "$SrcFw"/*; do
      [[ -e "$f" ]] || continue
      local base
      base="$(basename "$f")"
      if [[ "$base" == "WIFI.CFG" || "$base" == "WIFI_H.CFG" || "$base" == "WIFI_C.CFG" ]]; then
        if [[ -f "$Dest/FW/$base" ]]; then
          continue
        fi
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
    echo "FW/WIFI.CFG kept on TOYOS (not overwritten by sync)"
  else
    echo "note: no FW/WIFI.CFG on TOYOS — cp WIFI_H.CFG or WIFI_C.CFG → WIFI.CFG"
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      sed -n '2,16p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    --build|-b) DO_BUILD=1; shift ;;
    --kernel-only|-k) KERNEL_ONLY=1; shift ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

find_label_dev() {
  local Label="$1"
  blkid -L "$Label" 2>/dev/null || true
}

find_label_mnt() {
  local Label="$1"
  local Dev Mnt
  Dev="$(find_label_dev "$Label")"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -z "$Mnt" || "$Mnt" == "-" ]]; then
    return 1
  fi
  printf '%s\n' "$Mnt"
}

# 桌面常只挂 TOYOS、不挂 ESP；UEFI 却从 ESP 启动 → 只改 TOYOS/EFI 等于白改。
# 若 LABEL 存在但未挂载，挂到 /mnt/toyos-{esp,data}。
ensure_label_mounted() {
  local Label="$1"
  local DefaultMnt="$2"
  local Dev Mnt MUID MGID
  Dev="$(find_label_dev "$Label")"
  if [[ -z "$Dev" ]]; then
    return 1
  fi
  Mnt="$(lsblk -ln -o MOUNTPOINT "$Dev" 2>/dev/null | awk 'NF{print; exit}')"
  if [[ -n "$Mnt" && "$Mnt" != "-" ]]; then
    printf '%s\n' "$Mnt"
    return 0
  fi
  MUID="$(id -u)"
  MGID="$(id -g)"
  mkdir -p "$DefaultMnt"
  if mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  if command -v sudo >/dev/null 2>&1 && \
     sudo mount -o "uid=${MUID},gid=${MGID},umask=022" "$Dev" "$DefaultMnt" 2>/dev/null; then
    echo "mounted LABEL=$Label $Dev -> $DefaultMnt (sudo)" >&2
    printf '%s\n' "$DefaultMnt"
    return 0
  fi
  echo "error: found LABEL=$Label at $Dev but cannot mount (need root for ESP)." >&2
  return 1
}

ESP_MNT="${TOY_ESP_MNT:-}"
TOY_MNT="${TOY_TOYOS_MNT:-}"

if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_label_mnt ESP || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(find_label_mnt EFI || true)"
fi
if [[ -z "$TOY_MNT" ]]; then
  TOY_MNT="$(find_label_mnt TOYOS || true)"
fi

# 常见自动挂载路径兜底
if [[ -z "$TOY_MNT" ]]; then
  for d in /media/*/TOYOS /media/"${USER:-tank}"/TOYOS /run/media/"${USER:-tank}"/TOYOS; do
    if [[ -d "$d" ]]; then TOY_MNT="$d"; break; fi
  done
fi
if [[ -z "$ESP_MNT" ]]; then
  for d in /media/*/ESP /media/"${USER:-tank}"/ESP /run/media/"${USER:-tank}"/ESP \
           /media/*/EFI /media/"${USER:-tank}"/EFI; do
    if [[ -d "$d" ]]; then ESP_MNT="$d"; break; fi
  done
fi

# 未挂载则尝试挂上（尤其 ESP：否则 UEFI 仍跑旧 BOOTX64）
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted ESP /mnt/toyos-esp || true)"
fi
if [[ -z "$ESP_MNT" ]]; then
  ESP_MNT="$(ensure_label_mounted EFI /mnt/toyos-esp || true)"
fi
if [[ -z "$TOY_MNT" ]]; then
  TOY_MNT="$(ensure_label_mounted TOYOS /mnt/toyos-data || true)"
fi

if [[ -z "$TOY_MNT" ]]; then
  echo "error: TOYOS volume not mounted (label TOYOS)." >&2
  echo "  Format: $ROOT/make-usb-stick.sh --yes --sync" >&2
  echo "  Or mount the TOYOS partition and re-run." >&2
  exit 1
fi

# --kernel-only：只碰 TOYOS，不要求 ESP（避免误判 single-FAT / 往 ESP 写 Kernel）
if [[ "$KERNEL_ONLY" -eq 1 ]]; then
  if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "== build Kernel =="
    (cd "$ROOT/../ToyKernel" && ./build.sh)
  fi
  if [[ ! -d "$TOY_MNT" ]]; then
    echo "error: TOYOS mount missing: $TOY_MNT" >&2
    exit 1
  fi
  if [[ ! -w "$TOY_MNT" ]]; then
    echo "error: TOYOS not writable: $TOY_MNT (try: sudo chown \$USER \"$TOY_MNT\" or sudo $0 --kernel-only)" >&2
    exit 1
  fi
  SRC=""
  for C in \
    "$(toyos_kernel_build_dir "$(toyos_resolve_root "$0")")/HAL/X64/Kernel.elf" \
    "$ROOT/RootFs/X64/Kernel.elf"
  do
    if [[ -f "$C" ]]; then SRC="$C"; break; fi
  done
  if [[ -z "$SRC" ]]; then
    echo "error: no Kernel.elf — build first or drop --kernel-only" >&2
    exit 1
  fi
  if [[ -d "$TOY_MNT/EFI/BOOT" && ! -f "$TOY_MNT/TOYOS.ID" ]]; then
    echo "error: $TOY_MNT looks like ESP (EFI/BOOT present, no TOYOS.ID)." >&2
    echo "  Use the data partition (LABEL=TOYOS), e.g. /media/\$USER/TOYOS" >&2
    exit 1
  fi
  cp -f "$SRC" "$TOY_MNT/Kernel.elf"
  sync_toyos_fw "$TOY_MNT"
  if [[ ! -f "$TOY_MNT/TOYOS.ID" ]]; then
    printf "ToyOS root volume\n" > "$TOY_MNT/TOYOS.ID"
    echo "note: wrote $TOY_MNT/TOYOS.ID (UEFI Boot 靠它认系统盘)"
  fi
  sync
  echo "=== kernel-only sync (TOYOS only; ESP untouched) ==="
  echo "TOYOS -> $TOY_MNT"
  echo "src   -> $SRC"
  ls -l --time-style=long-iso "$SRC" "$TOY_MNT/Kernel.elf" "$TOY_MNT/TOYOS.ID"
  ls -l --time-style=long-iso "$TOY_MNT/FW/IWL8265.UCODE" 2>/dev/null || \
    echo "note: FW/IWL8265.UCODE missing on TOYOS (wifi fw=miss)"
  ls -l --time-style=long-iso "$TOY_MNT/FW/WIFI.CFG" 2>/dev/null || \
    echo "note: FW/WIFI.CFG missing — cfg=miss until you create it on the stick"
  md5sum "$SRC" "$TOY_MNT/Kernel.elf"
  echo
  echo "Boot loads Kernel from TOYOS via TOYOS.ID. Do not put Kernel.elf on ESP."
  echo "PHOTO 'fs: default=ESP (no TOYOS.ID)' is often the PC's NVMe ESP — kernel may not see USB yet."
  exit 0
fi

if [[ ! -d "$TOY_MNT" || ! -w "$TOY_MNT" ]]; then
  echo "error: TOYOS mount not writable: $TOY_MNT" >&2
  exit 1
fi

SINGLE_FAT=0
ESP_DEV="$(find_label_dev ESP)"
if [[ -z "$ESP_DEV" ]]; then
  ESP_DEV="$(find_label_dev EFI)"
fi
# 桌面常把 ESP 挂在 /mnt/toyos-esp，但 blkid 无 LABEL=ESP
if [[ -z "$ESP_MNT" && -d /mnt/toyos-esp/EFI/BOOT ]]; then
  ESP_MNT=/mnt/toyos-esp
  echo "note: using /mnt/toyos-esp as ESP (no LABEL=ESP in blkid)"
fi
if [[ -z "$ESP_MNT" ]]; then
  if [[ -n "$ESP_DEV" ]]; then
    echo "error: USB has ESP/EFI partition ($ESP_DEV) but it is not mounted." >&2
    echo "  UEFI boots from ESP — writing Boot only into TOYOS will NOT take effect." >&2
    echo "  Fix: sudo mount $ESP_DEV /mnt/toyos-esp && TOY_ESP_MNT=/mnt/toyos-esp $0" >&2
    exit 1
  fi
  # 同盘另一分区已挂成 TOYOS 时，勿把 TOYOS 当成 single-FAT ESP
  if lsblk -n -o NAME,MOUNTPOINT 2>/dev/null | grep -q toyos-esp; then
    echo "error: looks like dual-partition USB but ESP mount not resolved." >&2
    echo "  Mount ESP and re-run, or: TOY_ESP_MNT=/mnt/toyos-esp $0" >&2
    exit 1
  fi
  SINGLE_FAT=1
  ESP_MNT="$TOY_MNT"
  echo "note: no ESP partition — writing EFI into TOYOS (single-FAT layout)"
elif [[ ! -w "$ESP_MNT" ]]; then
  echo "error: ESP mount not writable: $ESP_MNT" >&2
  exit 1
fi

echo "ESP   -> $ESP_MNT"
echo "TOYOS -> $TOY_MNT"

if [[ "$DO_BUILD" -eq 1 ]]; then
  echo "== build Kernel =="
  (cd "$ROOT/../ToyKernel" && ./build.sh)
  echo "== build Boot =="
  if [[ -x "$ROOT/../ToyBoot/build.sh" ]]; then
    (cd "$ROOT/../ToyBoot" && ./build.sh)
  else
    echo "warning: ToyBoot/build.sh missing; keep existing BOOTX64.EFI" >&2
  fi
fi

echo "== prepare-rootfs =="
"$SCRIPT_DIR/prepare-rootfs.sh"

BOOT_EFI="$ROOT/Esp/X64/EFI/BOOT/BOOTX64.EFI"
if [[ ! -f "$BOOT_EFI" ]]; then
  echo "error: missing $BOOT_EFI — build ToyBoot" >&2
  exit 1
fi

echo "== sync ESP (Boot) =="
mkdir -p "$ESP_MNT/EFI/BOOT"
cp -f "$BOOT_EFI" "$ESP_MNT/EFI/BOOT/BOOTX64.EFI"
# 可选：若仓库有 startup.nsh 等可在此追加
sync

echo "== sync TOYOS (RootFs/X64) =="
# FAT 无 Unix owner/mode；勿用纯 -a（会 chown 失败 → exit 23）
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
  # 粗同步：先拷文件，不 --delete（避免误删用户在 U 盘上的笔记）
  cp -a "$ROOT/RootFs/X64/." "$TOY_MNT/"
fi
# 显式再扫一眼 FW/（rsync 已含；WIFI*.CFG 在 U 盘侧保留）
sync_toyos_fw "$TOY_MNT"

if [[ "$SINGLE_FAT" -eq 1 ]]; then
  # 仅单分区盘：启动与系统同卷，EFI 只能放 TOYOS
  mkdir -p "$TOY_MNT/EFI/BOOT"
  cp -f "$BOOT_EFI" "$TOY_MNT/EFI/BOOT/BOOTX64.EFI"
else
  # 双分区：EFI 只在 ESP；清掉 TOYOS 上误放的 EFI（旧兜底遗留，UEFI 不读它）
  if [[ -d "$TOY_MNT/EFI" ]]; then
    echo "note: removing stale $TOY_MNT/EFI (Boot lives on ESP only)"
    rm -rf "$TOY_MNT/EFI"
  fi
fi

# 确保识别文件存在
if [[ ! -f "$TOY_MNT/TOYOS.ID" ]]; then
  printf "ToyOS root volume\n" > "$TOY_MNT/TOYOS.ID"
fi

sync
echo
echo "=== sync done ==="
echo "ESP BOOTX64.EFI : $(stat -c%s "$ESP_MNT/EFI/BOOT/BOOTX64.EFI") bytes"
echo "TOYOS Kernel    : $(stat -c%s "$TOY_MNT/Kernel.elf") bytes"
ls -lh "$TOY_MNT/Kernel.elf" "$TOY_MNT/TOYOS.ID" "$TOY_MNT/THEME.CFG" 2>/dev/null || true
ls -lh "$TOY_MNT/FW/IWL8265.UCODE" 2>/dev/null || \
  echo "warning: TOYOS missing FW/IWL8265.UCODE (iwl fw=miss)"
ls -lh "$TOY_MNT/FW/WIFI.CFG" "$TOY_MNT/FW/WIFI_H.CFG" "$TOY_MNT/FW/WIFI_C.CFG" 2>/dev/null || \
  echo "note: TOYOS missing FW/WIFI.CFG (iwl cfg=miss; cp WIFI_H or WIFI_C → WIFI.CFG)"
echo
echo "Boot Menu: select this USB (UEFI). Expect GOP desktop / ToyOS ready."
echo "FW/WIFI*.CFG on the stick is preserved across sync (PSK stays on USB only)."
