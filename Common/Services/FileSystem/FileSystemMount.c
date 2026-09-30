/*
 * FileSystemMount.c — 扫描并挂上各卷（编排；PR-F-fs-1）
 * 单卷：FileSystemMountVol.c；默认卷：FileSystemMountPick.c
 */
#include "FileSystemPrivate.h"
#include "Block.h"
#include "BlockMux.h"
#include "Gpt.h"
#include "ToySerialLog.h"

/*
 * 扫描所有 Block 盘，挂上每个盘上的全部 FAT 分区；
 * 含 TOYOS.ID 的另名 TOYOS（默认）；GPT ESP 只读且可名 ESP。
 */
int MountAllVolumes(void) {
    UINT32 d;
    UINT8 Tmp[64];
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
            FsMountOneFatPart(d, &Parts[p], &ToyVol);
        }
    }

    FsMountAddResVolume();

    if (gVolCount <= 0) {
        return 0;
    }
    FsMountPickDefault(ToyVol, Tmp, sizeof(Tmp));
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
