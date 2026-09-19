/*
 * FatDirStress.c — 大目录回归（PR-S-fatdir-1）
 */
#include "Fat.h"
#include "FatPriv.h"
#include "Block.h"
#include "LibWrite.h"

/*
 * PR-F3：大目录回归。
 * MaxFiles > 0：最多创建 MaxFiles 个短名文件（F%04d.TXT）。
 * MaxFiles == 0：默认 24 个（课堂 vvfat 安全；验证多文件目录可读）。
 * MaxFiles < 0：强制 DirGrow（长名填簇）；真 FAT/virt 镜像可用，
 *               QEMU fat:rw(vvfat) 扩展目录常宿主断言失败。
 */
int FatDirStress(const char *DirPath, int MaxFiles, int *OutCreated, int *OutGrew) {
    FAT_DIR_CTX Dir;
    int Max0;
    int Created = 0;
    int Grew = 0;
    int Cap;
    int ForceGrow;
    int UseLong;
    int i;
    int Err;
    static UINT8 One[1] = { 'x' };
    char Path[FAT_NAME_MAX + 64];
    char Leaf[80];
    int DirLen;
    int p;
    UINT32 Spots;

    if (OutCreated) {
        *OutCreated = 0;
    }
    if (OutGrew) {
        *OutGrew = 0;
    }
    if (!DirPath || !DirPath[0]) {
        return FAT_ERR_INVAL;
    }

    ForceGrow = (MaxFiles < 0) ? 1 : 0;
    if (ForceGrow) {
        MaxFiles = -MaxFiles;
        if (MaxFiles <= 1) {
            MaxFiles = 0;
        }
    }
    UseLong = ForceGrow;

    Err = FatMkdir(DirPath);
    if (Err != FAT_OK && Err != FAT_ERR_EXIST) {
        return Err;
    }
    if (!ResolvePathAsDir(DirPath, &Dir)) {
        return FAT_ERR_NOENT;
    }
    if (Dir.IsFat16Root) {
        return FAT_ERR_INVAL;
    }
    Max0 = DirMaxIndex(Dir);

    Spots = ClusterBytes() / 32u / (UseLong ? 5u : 2u);
    if (Spots < 4u) {
        Spots = 4u;
    }

    if (MaxFiles > 0) {
        Cap = MaxFiles;
    } else if (ForceGrow) {
        Cap = (int)Spots + 8;
    } else {
        Cap = 24; /* vvfat 安全默认 */
    }
    if (Cap > 1024) {
        Cap = 1024;
    }

    DirLen = 0;
    while (DirPath[DirLen] && DirLen < FAT_NAME_MAX) {
        DirLen++;
    }

    for (i = 0; i < Cap; i++) {
        int n;
        int MaxNow;

        if (UseLong) {
            Leaf[0] = 'D';
            Leaf[1] = (char)('0' + ((i / 1000) % 10));
            Leaf[2] = (char)('0' + ((i / 100) % 10));
            Leaf[3] = (char)('0' + ((i / 10) % 10));
            Leaf[4] = (char)('0' + (i % 10));
            for (n = 5; n < 45; n++) {
                Leaf[n] = 'X';
            }
            Leaf[45] = '.';
            Leaf[46] = 'T';
            Leaf[47] = 'X';
            Leaf[48] = 'T';
            Leaf[49] = 0;
        } else {
            Leaf[0] = 'F';
            Leaf[1] = (char)('0' + ((i / 1000) % 10));
            Leaf[2] = (char)('0' + ((i / 100) % 10));
            Leaf[3] = (char)('0' + ((i / 10) % 10));
            Leaf[4] = (char)('0' + (i % 10));
            Leaf[5] = '.';
            Leaf[6] = 'T';
            Leaf[7] = 'X';
            Leaf[8] = 'T';
            Leaf[9] = 0;
        }

        p = 0;
        for (n = 0; n < DirLen && p < (int)sizeof(Path) - 2; n++) {
            Path[p++] = DirPath[n];
        }
        Path[p++] = '/';
        for (n = 0; Leaf[n] && p < (int)sizeof(Path) - 1; n++) {
            Path[p++] = Leaf[n];
        }
        Path[p] = 0;

        Err = FatWriteFile(Path, One, 1);
        if (Err != FAT_OK) {
            if (Created > 0 && (!ForceGrow || Grew)) {
                break;
            }
            if (OutCreated) {
                *OutCreated = Created;
            }
            if (OutGrew) {
                *OutGrew = Grew;
            }
            return Err;
        }
        Created++;

        if (!ResolvePathAsDir(DirPath, &Dir)) {
            return FAT_ERR_IO;
        }
        MaxNow = DirMaxIndex(Dir);
        if (MaxNow > Max0) {
            Grew = 1;
            if (ForceGrow) {
                break;
            }
        }
    }

    if (Created > 0) {
        UINT8 Buf[4];
        UINTN Sz = 0;
        int n;
        const char *First = UseLong
            ? "D0000XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX.TXT"
            : "F0000.TXT";
        p = 0;
        for (n = 0; n < DirLen && p < (int)sizeof(Path) - 2; n++) {
            Path[p++] = DirPath[n];
        }
        Path[p++] = '/';
        for (n = 0; First[n] && p < (int)sizeof(Path) - 1; n++) {
            Path[p++] = First[n];
        }
        Path[p] = 0;
        Err = FatReadFile(Path, Buf, sizeof(Buf), &Sz);
        if (Err != FAT_OK || Sz != 1 || Buf[0] != 'x') {
            return (Err != FAT_OK) ? Err : FAT_ERR_IO;
        }
    }

    if (OutCreated) {
        *OutCreated = Created;
    }
    if (OutGrew) {
        *OutGrew = Grew;
    }
    if (Created <= 0) {
        return FAT_ERR_NOSPC;
    }
    if (ForceGrow && !Grew) {
        return FAT_ERR_NOSPC;
    }
    return FAT_OK;
}
