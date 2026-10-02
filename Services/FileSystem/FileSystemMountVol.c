/*
 * FileSystemMountVol.c — 单 FAT 分区挂载 + RES（PR-F-fs-1）
 * 核心：FileSystemMount.c
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "Vfs.h"
#include "Debug.h"
#include "ToySerialLog.h"

static void NameEspSlot(FS_VOLUME *V, int BeforeCount, int SkipIdx) {
    int EspN = 0;
    int j;

    for (j = 0; j < BeforeCount; j++) {
        if (j == SkipIdx) {
            continue;
        }
        if (gVols[j].Name[0] == 'E' && gVols[j].Name[1] == 'S' &&
            gVols[j].Name[2] == 'P') {
            EspN++;
        }
    }
    if (EspN == 0) {
        CopyName(V->Name, FS_VOL_NAME_MAX, "ESP");
    } else {
        V->Name[0] = 'E';
        V->Name[1] = 'S';
        V->Name[2] = 'P';
        V->Name[3] = (char)('0' + (EspN + 1 > 9 ? 9 : EspN + 1));
        V->Name[4] = 0;
    }
    V->ReadOnly = 1;
}

static void NameToyIdVol(FS_VOLUME *V, UINT32 Drive, int Idx, int *ToyVol) {
    int Msc = BlockDriveIsMsc(Drive);
    int Take = 0;

    V->HasToyId = 1;
    /*
     * 多份 TOYOS.ID：默认卷跟内置盘（NVMe/AHCI），Store 装卸不落 U 盘。
     * 仅 U 盘有标记时仍名 TOYOS。U 盘那份改名 USB:。
     */
    if (*ToyVol < 0) {
        Take = 1;
    } else if (!Msc && BlockDriveIsMsc(gVols[*ToyVol].Drive)) {
        CopyName(gVols[*ToyVol].Name, FS_VOL_NAME_MAX, "USB");
        Take = 1;
        ToyLogFs("Fs: Prefer Internal TOYOS Over USB\n");
    } else if (Msc && !BlockDriveIsMsc(gVols[*ToyVol].Drive)) {
        CopyName(V->Name, FS_VOL_NAME_MAX, "USB");
        ToyLogFs("Fs: USB TOYOS Kept As USB:\n");
    } else {
        Take = 1;
    }
    if (Take) {
        CopyName(V->Name, FS_VOL_NAME_MAX, "TOYOS");
        *ToyVol = Idx;
    }
}

static int PickVolSlot(int HasId) {
    if (gVolCount < FS_MAX_VOLUMES) {
        return gVolCount;
    }
    /*
     * 卷表满：仍须留下 TOYOS.ID。挤掉最后一个非 TOYOS、非 RES 槽；
     * 否则 NVMe 上多个 ESP/OEM FAT 会先占满，USB 系统卷永远挂不上。
     */
    if (!HasId) {
        ToyLogFs("Fs: Skip Fat (Table Full)\n");
        return -1;
    }
    {
        int j;
        for (j = gVolCount - 1; j >= 0; j--) {
            if (gVols[j].HasToyId) {
                continue;
            }
            if (gVols[j].Ops && gVols[j].Ops->Synthetic) {
                continue;
            }
            ToyLogFs("Fs: Displace Vol For TOYOS.ID\n");
            return j;
        }
    }
    ToyLogFs("Fs: Skip TOYOS (No Slot To Displace)\n");
    return -1;
}

/* 挂上 Drive 上一个 FAT 分区；成功记入 gVols，并可能更新 *ToyVol */
void FsMountOneFatPart(UINT32 Drive, const GPT_FAT_PART *Part, int *ToyVol) {
    FS_VOLUME *V;
    int Idx;
    int IsEsp;
    UINT32 Start;
    int HasId = 0;
    UINT8 Tmp[64];
    UINTN Sz;

    if (!Part || !ToyVol) {
        return;
    }
    IsEsp = Part->IsEsp;
    Start = Part->StartLba;

    if (VfsSelect(FatFsOps()) != 0) {
        return;
    }
    if (!BlockSelect(Drive) || VfsMount(Start) != FAT_OK) {
        return;
    }
    if (VfsReadFile("TOYOS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
        HasId = 1;
    }

    Idx = PickVolSlot(HasId);
    if (Idx < 0) {
        return;
    }

    V = &gVols[Idx];
    V->Drive = Drive;
    V->StartLba = Start;
    V->Letter = (char)('A' + Idx);
    V->ReadOnly = IsEsp ? 1 : 0;
    V->HasToyId = 0;
    V->Ops = FatFsOps();
    V->Name[0] = V->Letter;
    V->Name[1] = 0;

    if (HasId) {
        NameToyIdVol(V, Drive, Idx, ToyVol);
    } else if (IsEsp) {
        NameEspSlot(V, gVolCount, Idx);
    }

    if (Idx == gVolCount) {
        gVolCount++;
    }
    gActiveVol = Idx;
    gActiveOps = V->Ops;
    gActiveDrive = Drive;
    gActiveLba = Start;

    DebugWrite("Fs: vol ");
    DebugWrite(V->Name);
    DebugWrite(" letter=");
    DebugHex32((UINT32)(UINT8)V->Letter);
    DebugWrite(" drive=");
    DebugHex32(Drive);
    DebugWrite(" lba=");
    DebugHex32(Start);
    DebugWrite("\n");
}

void FsMountAddResVolume(void) {
    FS_VOLUME *V;
    int Idx;

    if (gVolCount >= FS_MAX_VOLUMES || !ResFsOps()) {
        return;
    }
    V = &gVols[gVolCount];
    Idx = gVolCount;
    V->Drive = 0xFFFFFFFEu;
    V->StartLba = 0;
    V->Letter = (char)('A' + Idx);
    V->ReadOnly = 1;
    V->HasToyId = 0;
    V->Ops = ResFsOps();
    CopyName(V->Name, FS_VOL_NAME_MAX, "RES");
    if (VfsSelect(V->Ops) == 0 && VfsMount(0) == FAT_OK) {
        gVolCount++;
        gActiveVol = Idx;
        gActiveOps = V->Ops;
        gActiveDrive = V->Drive;
        gActiveLba = 0;
        DebugWrite("Fs: vol RES (resfs)\n");
        ToyLogFs("Fs: RES Volume RES:\n");
    }
}
