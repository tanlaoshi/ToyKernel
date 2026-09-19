/*
 * FileSystemInit.c — 重挂与启动（PR-S-filesystem-1）
 */
#include "FileSystemPrivate.h"
#include "Store.h"
#include "ShellCommands.h"
#include "Block.h"
#include "Gpt.h"
#include "Vfs.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "CoreOps.h"

int FileSystemRemountVolumes(void) {
    /*
     * 勿再 HalBlockInit()/ProbeClass：会重绑 AHCI 冲掉已装的 BlockMux。
     * 后端已在时只 BlockInit 重 Probe（Mux 会挂上 MSC 盘号）。
     */
    if (!BlockBackendReady()) {
        if (HalBlockInit() <= 0) {
            DebugWrite("FS: remount no block backend\n");
        }
    } else if (BlockInit() <= 0) {
        DebugWrite("FS: remount BlockInit found 0 drives\n");
    }
    if (!MountAllVolumes()) {
        DebugWrite("FS: remount no volumes\n");
        return 0;
    }
    return 1;
}

/* PR-H-msc-7b：卷上 MSC.OFF 或 THEME.CFG 含 msc=0 → 关 auto */
static int MscPolicySaysOff(void) {
    UINT8 Buf[512];
    UINTN Sz = 0;
    UINTN i;

    if (FileSystemReadFile("MSC.OFF", Buf, 1, &Sz) == FAT_OK) {
        return 1;
    }
    Sz = 0;
    if (FileSystemReadFile("THEME.CFG", Buf, sizeof(Buf) - 1, &Sz) != FAT_OK) {
        return 0;
    }
    Buf[Sz] = 0;
    for (i = 0; i + 5 <= Sz; i++) {
        if (Buf[i] == 'm' && Buf[i + 1] == 's' && Buf[i + 2] == 'c' &&
            Buf[i + 3] == '=' && Buf[i + 4] == '0') {
            return 1;
        }
    }
    return 0;
}

static int AnyVolumeHasToyId(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].HasToyId) {
            return 1;
        }
    }
    return 0;
}

int FileSystemInitialize(void) {
    VFS_SERVICE_OPS Svc;
    int Auto;
    int HaveVols = 0;
    int MuxOk = 0;
    int HasToy = 0;

    if (VfsRegister(FatFsOps()) != 0) {
        DebugWrite("FS: VfsRegister(fat) failed\n");
        return 0;
    }
    if (VfsRegister(ResFsOps()) != 0) {
        DebugWrite("FS: VfsRegister(res) failed\n");
        return 0;
    }

    /*
     * PR-H-msc-7b：Live 默认 FS 前 auto（claim→Mux），再一次 MountAllVolumes。
     * 主盘已有 TOYOS 且 MSC.OFF / msc=0 → 可关 auto。
     * 主盘只有 ESP、尚无 TOYOS.ID 时不得关 auto（NUC Live：否则永远读不到 U 盘 TOYOS）。
     */
    Auto = HalUsbMscAutoEnabled();
    if (HalBlockInit() > 0 && MountAllVolumes()) {
        HaveVols = 1;
        HasToy = AnyVolumeHasToyId();
        if (Auto && MscPolicySaysOff()) {
            if (HasToy) {
                Auto = 0;
                HalUsbMscAutoSet(0);
                ToyLogBoot("Boot: MSC Auto Off (MSC=0/MSC.OFF)\n");
            } else {
                ToyLogBoot(
                    "Boot: MSC Auto Keep (No TOYOS.ID; Ignore MSC=0)\n");
            }
        }
    } else {
        DebugWrite("FS: no primary volumes yet (Live USB path ok)\n");
    }

    if (!HasToy && !Auto) {
        Auto = 1;
        HalUsbMscAutoSet(1);
        ToyLogBoot("Boot: MSC Auto Force (Need TOYOS)\n");
    }

    if (Auto) {
        if (HalUsbMscAutoBeforeFs() == 0) {
            MuxOk = 1;
            if (BlockInit() <= 0) {
                DebugWrite("FS: BlockInit after msc auto failed\n");
            }
            if (!MountAllVolumes()) {
                DebugWrite("FS: mount after msc auto failed\n");
                if (!HaveVols) {
                    return 0;
                }
            } else {
                HaveVols = 1;
                HasToy = AnyVolumeHasToyId();
            }
        } else if (!HasToy) {
            /*
             * Live U 盘上电慢：首轮 claim 空 → 只见 NVMe ESP。
             * 再试一轮（claim 内已有 Force/等待；勿在此拖很久）。
             */
            ToyLogBoot("Boot: MSC Auto Retry (No TOYOS)\n");
            if (HalUsbMscAutoBeforeFs() == 0) {
                MuxOk = 1;
                if (BlockInit() > 0 && MountAllVolumes()) {
                    HaveVols = 1;
                    HasToy = AnyVolumeHasToyId();
                }
            }
        }
    }

    if (!HaveVols && !MuxOk) {
        if (HalBlockInit() <= 0) {
            DebugWrite("FS: no block device (RES-only possible)\n");
        }
        if (!MountAllVolumes()) {
            DebugWrite("FS: no volumes mounted\n");
            return 0;
        }
    } else if (!HaveVols) {
        DebugWrite("FS: no volumes mounted\n");
        return 0;
    }

    Svc.ReadFile = FileSystemReadFile;
    Svc.WriteFile = FileSystemWriteFile;
    Svc.ListEntries = FileSystemListEntries;
    Svc.FileStat = FileSystemFileStat;
    Svc.ReadFileAt = FileSystemReadFileAt;
    Svc.WriteFileAt = FileSystemWriteFileAt;
    VfsServiceOpsRegister(&Svc);
    ShellCommandsRegisterFs();
    DebugWrite("FS ready (ls, cat, write, wrbig, dirstress, rm, mkdir, rmdir, mv, vols, filestat, filesync)\n");
    if (!AnyVolumeHasToyId()) {
        HalConsoleWriteSerial("Fs: WARN no TOYOS.ID on any volume\n");
    }
    return 0;
}
