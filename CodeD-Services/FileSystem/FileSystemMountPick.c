/*
 * FileSystemMountPick.c — 选默认卷 / 无名 ESP 命名（PR-F-fs-1）
 * 核心：FileSystemMount.c
 */
#include "FileSystemPrivate.h"
#include "Vfs.h"
#include "ToySerialLog.h"

static void RenameEspIfLetterOnly(int i) {
    int NeedEspName = 0;
    int EspN = 0;
    int j;

    gVols[i].ReadOnly = 1;
    if (!(gVols[i].Name[0] && gVols[i].Name[1])) {
        NeedEspName = 1;
    } else if (gVols[i].Name[0] == gVols[i].Letter && gVols[i].Name[1] == 0) {
        NeedEspName = 1;
    }
    if (!NeedEspName) {
        return;
    }
    for (j = 0; j < i; j++) {
        if (gVols[j].Name[0] == 'E' && gVols[j].Name[1] == 'S' &&
            gVols[j].Name[2] == 'P') {
            EspN++;
        }
    }
    if (EspN == 0) {
        CopyName(gVols[i].Name, FS_VOL_NAME_MAX, "ESP");
    } else {
        gVols[i].Name[0] = 'E';
        gVols[i].Name[1] = 'S';
        gVols[i].Name[2] = 'P';
        gVols[i].Name[3] = (char)('0' + (EspN + 1 > 9 ? 9 : EspN + 1));
        gVols[i].Name[4] = 0;
    }
}

static void PickWithToyId(int ToyVol) {
    int i;

    gDefaultVol = ToyVol;
    /* 有 TOYOS 卷时，其余 Block 卷视为启动/ESP：只读，名 ESP（仍可用 A:） */
    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].Ops && gVols[i].Ops->Synthetic) {
            continue;
        }
        if (!gVols[i].HasToyId) {
            RenameEspIfLetterOnly(i);
        }
    }
}

static void PickWithoutToyId(UINT8 *Tmp, UINTN TmpSz) {
    int i;
    int FatVol = -1;
    int PrefVol = -1;
    int MarkerVol = -1;
    UINTN Sz;

    for (i = 0; i < gVolCount; i++) {
        if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
            continue;
        }
        if (FatVol < 0) {
            FatVol = i;
        }
        if (!gVols[i].ReadOnly && PrefVol < 0) {
            PrefVol = i;
        }
        if (MarkerVol < 0 && FileSystemActivate(i) == FAT_OK) {
            if (VfsReadFile("HELLO.ELF", Tmp, TmpSz, &Sz) == FAT_OK ||
                VfsReadFile("Kernel.elf", Tmp, TmpSz, &Sz) == FAT_OK ||
                VfsReadFile("TOYOS.DB", Tmp, TmpSz, &Sz) == FAT_OK) {
                MarkerVol = i;
            }
        }
    }
    if (MarkerVol >= 0) {
        gDefaultVol = MarkerVol;
        CopyName(gVols[MarkerVol].Name, FS_VOL_NAME_MAX, "TOYOS");
        ToyLogFs("Fs: Named TOYOS by marker (No TOYOS.ID)\n");
    } else if (PrefVol >= 0) {
        gDefaultVol = PrefVol;
    } else {
        gDefaultVol = (FatVol >= 0) ? FatVol : 0;
    }
    if (gDefaultVol >= 0 && gDefaultVol < gVolCount &&
        gVols[gDefaultVol].Name[0] == 'E' && gVols[gDefaultVol].Name[1] == 'S' &&
        gVols[gDefaultVol].Name[2] == 'P') {
        ToyLogFs(
            "Fs: Default=ESP (No TOYOS.ID; Put Rootfs On A FAT With TOYOS.ID)\n");
    }
}

void FsMountPickDefault(int ToyVol, UINT8 *Tmp, UINTN TmpSz) {
    if (ToyVol >= 0) {
        PickWithToyId(ToyVol);
    } else {
        PickWithoutToyId(Tmp, TmpSz);
    }
}
