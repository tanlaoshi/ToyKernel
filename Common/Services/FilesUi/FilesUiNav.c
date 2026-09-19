/*
 * FilesUiNav.c — Files 路径/卷侧栏/预览
 *
 * 侧栏按 FileSystem 已挂载卷动态生成；TOYOS 置顶；有 TOYOS 时附 Apps/Assets。
 */
#include "FilesUiPriv.h"
#include "Fat.h"

static void AppendDriveTag(char *Label, int Max, UINT32 Drive) {
    int n = 0;
    char Dig[4];
    int Dn = 0;
    UINT32 V;

    if (Max <= 0 || !Label) {
        return;
    }
    while (Label[n]) {
        n++;
    }
    if (n + 4 >= Max) {
        return;
    }
    Label[n++] = ' ';
    Label[n++] = 'd';
    V = Drive;
    if (V == 0) {
        Dig[Dn++] = '0';
    } else {
        while (V > 0 && Dn < (int)sizeof(Dig)) {
            Dig[Dn++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (Dn > 0 && n < Max - 1) {
        Label[n++] = Dig[--Dn];
    }
    Label[n] = 0;
}

static int NameIsToy(const char *Name) {
    return Name && Name[0] == 'T' && Name[1] == 'O' && Name[2] == 'Y' &&
           (Name[3] == 'O' || Name[3] == 'o') &&
           (Name[4] == 'S' || Name[4] == 's') && Name[5] == 0;
}

static int NameIsRes(const char *Name) {
    return Name && (Name[0] == 'R' || Name[0] == 'r') &&
           (Name[1] == 'E' || Name[1] == 'e') &&
           (Name[2] == 'S' || Name[2] == 's') && Name[3] == 0;
}

static int NameIsEsp(const char *Name) {
    return Name && (Name[0] == 'E' || Name[0] == 'e') &&
           (Name[1] == 'S' || Name[1] == 's') &&
           (Name[2] == 'P' || Name[2] == 'p');
}

void RebuildPlaces(void) {
    int N;
    int i;
    int j;
    int PlaceN = 0;
    int MarkDrive = 0;
    int ToyVol = -1;
    int EspVol = -1;
    UINT32 ToyDrive = 0;
    UINT32 EspDrive = 0;
    char Names[FS_MAX_VOLUMES][FS_VOL_NAME_MAX];
    UINT32 Drives[FS_MAX_VOLUMES];
    char Letters[FS_MAX_VOLUMES];
    int Order[FS_MAX_VOLUMES];
    int VolN = 0;

    gPlaceCount = 0;
    N = FileSystemVolCount();
    if (N > FS_MAX_VOLUMES) {
        N = FS_MAX_VOLUMES;
    }

    for (i = 0; i < N; i++) {
        UINT32 Drive = 0;
        int Ro = 0;

        Names[VolN][0] = 0;
        if (FileSystemVolInfo(i, Names[VolN], FS_VOL_NAME_MAX, &Drive, 0, &Ro) != 0) {
            continue;
        }
        Drives[VolN] = Drive;
        Letters[VolN] = (char)('A' + i);
        Order[VolN] = VolN;
        if (NameIsToy(Names[VolN])) {
            ToyVol = VolN;
            ToyDrive = Drive;
        }
        if (EspVol < 0 && NameIsEsp(Names[VolN])) {
            EspVol = VolN;
            EspDrive = Drive;
        }
        VolN++;
    }

    /* ESP 与 TOYOS 不在同一盘 → 侧栏标 dN */
    if (ToyVol >= 0 && EspVol >= 0 && ToyDrive != EspDrive &&
        ToyDrive != 0xFFFFFFFEu && EspDrive != 0xFFFFFFFEu) {
        MarkDrive = 1;
    }
    for (i = 0; i < VolN && !MarkDrive; i++) {
        for (j = i + 1; j < VolN; j++) {
            if (FilesUiStrEqIgnoreCase(Names[i], Names[j])) {
                MarkDrive = 1;
                break;
            }
        }
    }

    /* 排序：TOYOS 置顶，RES 垫底，其余保持挂载序 */
    for (i = 0; i < VolN; i++) {
        for (j = i + 1; j < VolN; j++) {
            int Ai = Order[i];
            int Aj = Order[j];
            int Swap = 0;

            if (NameIsToy(Names[Aj]) && !NameIsToy(Names[Ai])) {
                Swap = 1;
            } else if (!NameIsToy(Names[Ai]) && !NameIsToy(Names[Aj])) {
                if (NameIsRes(Names[Ai]) && !NameIsRes(Names[Aj])) {
                    Swap = 1;
                }
            }
            if (Swap) {
                int T = Order[i];
                Order[i] = Order[j];
                Order[j] = T;
            }
        }
    }

    for (i = 0; i < VolN && PlaceN < FILES_PLACE_MAX; i++) {
        int V = Order[i];
        int k;
        int NameUnique = 1;

        for (j = 0; j < VolN; j++) {
            if (j != V && FilesUiStrEqIgnoreCase(Names[V], Names[j])) {
                NameUnique = 0;
                break;
            }
        }
        /* 名唯一 → TOYOS: / ESP:；撞名 → A: 等字母前缀 */
        if (NameUnique && Names[V][0]) {
            for (k = 0; Names[V][k] && k < FILES_PLACE_PATH_MAX - 2; k++) {
                gPlacePaths[PlaceN][k] = Names[V][k];
            }
            gPlacePaths[PlaceN][k++] = ':';
            gPlacePaths[PlaceN][k] = 0;
        } else {
            gPlacePaths[PlaceN][0] = Letters[V];
            gPlacePaths[PlaceN][1] = ':';
            gPlacePaths[PlaceN][2] = 0;
        }

        /* 标签带冒号，与旧书签 TOYOS: 观感一致 */
        for (k = 0; gPlacePaths[PlaceN][k] && k < FILES_PLACE_LABEL_MAX - 1; k++) {
            gPlaceLabels[PlaceN][k] = gPlacePaths[PlaceN][k];
        }
        gPlaceLabels[PlaceN][k] = 0;
        if (MarkDrive && Drives[V] != 0xFFFFFFFEu) {
            AppendDriveTag(gPlaceLabels[PlaceN], FILES_PLACE_LABEL_MAX, Drives[V]);
        }

        gPlaces[PlaceN].Label = gPlaceLabels[PlaceN];
        gPlaces[PlaceN].Path = gPlacePaths[PlaceN];
        PlaceN++;
    }

    /* 有 TOYOS 时恢复 Apps/Assets 捷径（旧 U2） */
    if (ToyVol >= 0 && PlaceN + 2 <= FILES_PLACE_MAX) {
        CopyStr(gPlaceLabels[PlaceN], FILES_PLACE_LABEL_MAX, "Apps/");
        CopyStr(gPlacePaths[PlaceN], FILES_PLACE_PATH_MAX, "TOYOS:Apps");
        gPlaces[PlaceN].Label = gPlaceLabels[PlaceN];
        gPlaces[PlaceN].Path = gPlacePaths[PlaceN];
        PlaceN++;

        CopyStr(gPlaceLabels[PlaceN], FILES_PLACE_LABEL_MAX, "Assets/");
        CopyStr(gPlacePaths[PlaceN], FILES_PLACE_PATH_MAX, "TOYOS:Assets");
        gPlaces[PlaceN].Label = gPlaceLabels[PlaceN];
        gPlaces[PlaceN].Path = gPlacePaths[PlaceN];
        PlaceN++;
    }

    gPlaceCount = PlaceN;
}

/* cwd 是否落在该侧栏项（卷根、卷内子路径、或 Apps/Assets） */
int PlaceMatches(int Idx) {
    const char *P;
    int n;
    int i;

    if (Idx < 0 || Idx >= gPlaceCount) {
        return 0;
    }
    P = gPlaces[Idx].Path;
    if (!P || !P[0]) {
        return 0;
    }
    if (PathEqIgnoreCase(gCwd, P)) {
        return 1;
    }

    n = 0;
    while (P[n]) {
        n++;
    }

    /* 空 cwd：仅默认卷的卷根项命中 */
    if (gCwd[0] == 0 && n > 0 && P[n - 1] == ':') {
        char DefName[FS_VOL_NAME_MAX];
        char DefPath[FS_VOL_NAME_MAX + 2];
        int Def = FileSystemDefaultVol();
        int L = 0;

        if (Def >= 0 &&
            FileSystemVolInfo(Def, DefName, (int)sizeof(DefName), 0, 0, 0) == 0 &&
            DefName[0]) {
            while (DefName[L] && L < FS_VOL_NAME_MAX - 1) {
                DefPath[L] = DefName[L];
                L++;
            }
            DefPath[L++] = ':';
            DefPath[L] = 0;
            if (PathEqIgnoreCase(P, DefPath)) {
                return 1;
            }
        }
        return 0;
    }

    /* 前缀匹配（大小写不敏感） */
    for (i = 0; P[i] && gCwd[i]; i++) {
        char Ca = P[i];
        char Cb = gCwd[i];
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
    }
    if (P[i] != 0) {
        return 0;
    }
    /* Path 耗尽：卷根 TOYOS: 匹配 TOYOS:Apps；目录捷径需边界 / 或结束 */
    if (n > 0 && P[n - 1] == ':') {
        return 1;
    }
    if (gCwd[i] == 0 || gCwd[i] == '/') {
        return 1;
    }
    return 0;
}

void SyncSideSel(void) {
    int i;
    gSideSel = -1;
    for (i = 0; i < gPlaceCount; i++) {
        if (PlaceMatches(i)) {
            gSideSel = i;
            break;
        }
    }
}
