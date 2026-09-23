/*
 * FileSystemMount.c — 扫描并挂上各卷（PR-S-filesystem-1）
 */
#include "FileSystemPrivate.h"
#include "Store.h"
#include "ShellCommands.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "Vfs.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "CoreOps.h"

/*
 * 扫描所有 Block 盘，挂上每个盘上的全部 FAT 分区；
 * 含 TOYOS.ID 的另名 TOYOS（默认）；GPT ESP 只读且可名 ESP。
 */
int MountAllVolumes(void) {
    UINT32 d;
    UINT8 Tmp[64];
    UINTN Sz;
    int ToyVol = -1;

    gVolCount = 0;
    gDefaultVol = 0;
    gActiveVol = -1;
    gActiveOps = 0;
    gActiveDrive = 0xFFFFFFFFu;
    gActiveLba = 0xFFFFFFFFu;

    for (d = 0; d < BLOCK_MAX_DRIVES && gVolCount < FS_MAX_VOLUMES; d++) {
        GPT_FAT_PART Parts[GPT_MAX_FAT_PARTS];
        int N;
        int p;

        if (!BlockSelect(d)) {
            continue;
        }
        N = GptFindAllFat(Parts, GPT_MAX_FAT_PARTS);
        if (N <= 0) {
            continue;
        }
        for (p = 0; p < N; p++) {
            FS_VOLUME *V;
            int Idx;
            int IsEsp = Parts[p].IsEsp;
            UINT32 Start = Parts[p].StartLba;
            int HasId = 0;

            if (VfsSelect(FatFsOps()) != 0) {
                continue;
            }
            if (!BlockSelect(d) || VfsMount(Start) != FAT_OK) {
                continue;
            }

            if (VfsReadFile("TOYOS.ID", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
                HasId = 1;
            }

            /*
             * 卷表满：仍须留下 TOYOS.ID。挤掉最后一个非 TOYOS、非 RES 槽；
             * 否则 NVMe 上多个 ESP/OEM FAT 会先占满，USB 系统卷永远挂不上。
             */
            if (gVolCount >= FS_MAX_VOLUMES) {
                if (!HasId) {
                    ToyLogFs("Fs: Skip Fat (Table Full)\n");
                    continue;
                }
                {
                    int Slot = -1;
                    int j;
                    for (j = gVolCount - 1; j >= 0; j--) {
                        if (gVols[j].HasToyId) {
                            continue;
                        }
                        if (gVols[j].Ops && gVols[j].Ops->Synthetic) {
                            continue;
                        }
                        Slot = j;
                        break;
                    }
                    if (Slot < 0) {
                        ToyLogFs("Fs: Skip TOYOS (No Slot To Displace)\n");
                        continue;
                    }
                    Idx = Slot;
                    ToyLogFs("Fs: Displace Vol For TOYOS.ID\n");
                }
            } else {
                Idx = gVolCount;
            }

            V = &gVols[Idx];
            V->Drive = d;
            V->StartLba = Start;
            V->Letter = (char)('A' + Idx);
            V->ReadOnly = IsEsp ? 1 : 0;
            V->HasToyId = 0;
            V->Ops = FatFsOps();
            V->Name[0] = V->Letter;
            V->Name[1] = 0;

            if (HasId) {
                int Msc = BlockDriveIsMsc(d);
                int Take = 0;

                V->HasToyId = 1;
                /*
                 * 多份 TOYOS.ID：默认卷跟内置盘（NVMe/AHCI），Store 装卸不落 U 盘。
                 * 仅 U 盘有标记时仍名 TOYOS。U 盘那份改名 USB:。
                 */
                if (ToyVol < 0) {
                    Take = 1;
                } else if (!Msc && BlockDriveIsMsc(gVols[ToyVol].Drive)) {
                    CopyName(gVols[ToyVol].Name, FS_VOL_NAME_MAX, "USB");
                    Take = 1;
                    ToyLogFs("Fs: Prefer Internal TOYOS Over USB\n");
                } else if (Msc && !BlockDriveIsMsc(gVols[ToyVol].Drive)) {
                    CopyName(V->Name, FS_VOL_NAME_MAX, "USB");
                    ToyLogFs("Fs: USB TOYOS Kept As USB:\n");
                } else {
                    Take = 1;
                }
                if (Take) {
                    CopyName(V->Name, FS_VOL_NAME_MAX, "TOYOS");
                    ToyVol = Idx;
                }
            } else if (IsEsp) {
                /* 多 ESP 时第二块起名 ESP2…，避免 Resolve(ESP:) 撞名 */
                int EspN = 0;
                int j;

                for (j = 0; j < gVolCount; j++) {
                    if (j == Idx) {
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

            if (Idx == gVolCount) {
                gVolCount++;
            }
            gActiveVol = Idx;
            gActiveOps = V->Ops;
            gActiveDrive = d;
            gActiveLba = Start;

            DebugWrite("Fs: vol ");
            DebugWrite(V->Name);
            DebugWrite(" letter=");
            DebugHex32((UINT32)(UINT8)V->Letter);
            DebugWrite(" drive=");
            DebugHex32(d);
            DebugWrite(" lba=");
            DebugHex32(Start);
            DebugWrite("\n");
        }
    }

    /* PR-F3：可选只读资源卷（无盘亦可挂；有盘时占下一字母） */
    if (gVolCount < FS_MAX_VOLUMES && ResFsOps()) {
        FS_VOLUME *V = &gVols[gVolCount];
        int Idx = gVolCount;

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

    if (gVolCount <= 0) {
        return 0;
    }
    if (ToyVol >= 0) {
        int i;
        gDefaultVol = ToyVol;
        /* 有 TOYOS 卷时，其余 Block 卷视为启动/ESP：只读，名 ESP（仍可用 A:） */
        for (i = 0; i < gVolCount; i++) {
            if (gVols[i].Ops && gVols[i].Ops->Synthetic) {
                continue; /* RES 等合成卷保持原名 */
            }
            if (!gVols[i].HasToyId) {
                int NeedEspName = 0;
                gVols[i].ReadOnly = 1;
                if (!(gVols[i].Name[0] && gVols[i].Name[1])) {
                    NeedEspName = 1;
                } else if (gVols[i].Name[0] == gVols[i].Letter &&
                           gVols[i].Name[1] == 0) {
                    NeedEspName = 1;
                }
                if (NeedEspName) {
                    int EspN = 0;
                    int j;
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
                        gVols[i].Name[3] =
                            (char)('0' + (EspN + 1 > 9 ? 9 : EspN + 1));
                        gVols[i].Name[4] = 0;
                    }
                }
            }
        }
    } else {
        int i;
        int FatVol = -1;
        int PrefVol = -1;
        int MarkerVol = -1;

        for (i = 0; i < gVolCount; i++) {
            if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
                continue;
            }
            if (FatVol < 0) {
                FatVol = i;
            }
            /* 无 TOYOS.ID 时优先非 ESP（可写数据分区） */
            if (!gVols[i].ReadOnly && PrefVol < 0) {
                PrefVol = i;
            }
            if (MarkerVol < 0 && FileSystemActivate(i) == FAT_OK) {
                if (VfsReadFile("HELLO.ELF", Tmp, sizeof(Tmp), &Sz) == FAT_OK ||
                    VfsReadFile("Kernel.elf", Tmp, sizeof(Tmp), &Sz) == FAT_OK ||
                    VfsReadFile("TOYOS.DB", Tmp, sizeof(Tmp), &Sz) == FAT_OK) {
                    MarkerVol = i;
                }
            }
        }
        if (MarkerVol >= 0) {
            gDefaultVol = MarkerVol;
            /* 无 TOYOS.ID 但有 Kernel/DB 标记 → 仍名 TOYOS，Files 侧栏可辨 */
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
    if (FileSystemActivate(gDefaultVol) != FAT_OK) {
        return 0;
    }

    ToyLogFs("Fs: Mounted ");
    {
        char Msg[8];
        Msg[0] = (char)('0' + (gVolCount > 9 ? 9 : gVolCount));
        Msg[1] = 0;
        ToyLogFs(Msg);
    }
    ToyLogFs(" volume(s), default=");
    ToyLogFs(gVols[gDefaultVol].Name);
    ToyLogFs("\n");
    return 1;
}
