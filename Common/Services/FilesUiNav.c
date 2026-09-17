/*
 * FilesUiNav.c — Files 路径/卷侧栏/预览
 *
 * 侧栏按 FileSystem 已挂载卷动态生成（无 Apps/Assets 占位）。
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

void RebuildPlaces(void) {
    int N;
    int i;
    int j;
    int MarkDrive = 0;
    int ToyIdx = -1;
    int EspIdx = -1;
    UINT32 ToyDrive = 0;
    UINT32 EspDrive = 0;
    char Names[FILES_PLACE_MAX][FS_VOL_NAME_MAX];
    UINT32 Drives[FILES_PLACE_MAX];
    char Letters[FILES_PLACE_MAX];

    gPlaceCount = 0;
    N = FileSystemVolCount();
    if (N > FILES_PLACE_MAX) {
        N = FILES_PLACE_MAX;
    }
    for (i = 0; i < N; i++) {
        UINT32 Drive = 0;
        int Ro = 0;
        Names[i][0] = 0;
        if (FileSystemVolInfo(i, Names[i], FS_VOL_NAME_MAX, &Drive, 0, &Ro) != 0) {
            continue;
        }
        Drives[i] = Drive;
        Letters[i] = (char)('A' + i);
        if (Names[i][0] == 'T' && Names[i][1] == 'O' && Names[i][2] == 'Y') {
            ToyIdx = i;
            ToyDrive = Drive;
        }
        if (Names[i][0] == 'E' && Names[i][1] == 'S' && Names[i][2] == 'P') {
            if (EspIdx < 0) {
                EspIdx = i;
                EspDrive = Drive;
            }
        }
    }

    /* ESP 与 TOYOS 不在同一盘 → 侧栏标 dN */
    if (ToyIdx >= 0 && EspIdx >= 0 && ToyDrive != EspDrive &&
        ToyDrive != 0xFFFFFFFEu && EspDrive != 0xFFFFFFFEu) {
        MarkDrive = 1;
    }
    /* 同名卷（如双 ESP）也标盘号 */
    for (i = 0; i < N && !MarkDrive; i++) {
        for (j = i + 1; j < N; j++) {
            if (FilesUiStrEqIgnoreCase(Names[i], Names[j])) {
                MarkDrive = 1;
                break;
            }
        }
    }

    for (i = 0; i < N; i++) {
        int k;
        int NameUnique = 1;

        for (j = 0; j < N; j++) {
            if (j != i && FilesUiStrEqIgnoreCase(Names[i], Names[j])) {
                NameUnique = 0;
                break;
            }
        }
        /* 名唯一 → TOYOS: / ESP:；撞名 → A: 等字母前缀 */
        if (NameUnique && Names[i][0]) {
            for (k = 0; Names[i][k] && k < FILES_PLACE_PATH_MAX - 2; k++) {
                gPlacePaths[i][k] = Names[i][k];
            }
            gPlacePaths[i][k++] = ':';
            gPlacePaths[i][k] = 0;
        } else {
            gPlacePaths[i][0] = Letters[i];
            gPlacePaths[i][1] = ':';
            gPlacePaths[i][2] = 0;
        }

        for (k = 0; Names[i][k] && k < FILES_PLACE_LABEL_MAX - 1; k++) {
            gPlaceLabels[i][k] = Names[i][k];
        }
        gPlaceLabels[i][k] = 0;
        if (MarkDrive && Drives[i] != 0xFFFFFFFEu) {
            AppendDriveTag(gPlaceLabels[i], FILES_PLACE_LABEL_MAX, Drives[i]);
        }

        gPlaces[i].Label = gPlaceLabels[i];
        gPlaces[i].Path = gPlacePaths[i];
        gPlaceCount++;
    }
}

/* 当前 cwd 是否落在该侧栏卷（卷根或卷内子路径） */
int PlaceMatches(int Idx) {
    const char *P;
    int n;
    int i;
    char Name[FS_VOL_NAME_MAX];
    char NamePath[FS_VOL_NAME_MAX + 2];

    if (Idx < 0 || Idx >= gPlaceCount) {
        return 0;
    }
    P = gPlaces[Idx].Path;
    if (PathEqIgnoreCase(gCwd, P)) {
        return 1;
    }
    /* 字母卷根 "A:" 匹配空 cwd 且该卷为默认卷 */
    n = 0;
    while (P[n]) {
        n++;
    }
    if (n == 2 && P[1] == ':' && gCwd[0] == 0 && Idx == FileSystemDefaultVol()) {
        return 1;
    }
    /* cwd 以 "A:" 或 "TOYOS:" 开头 */
    if (n >= 2 && P[1] == ':') {
        for (i = 0; i < 2 && gCwd[i]; i++) {
            char Ca = P[i];
            char Cb = gCwd[i];
            if (Ca >= 'A' && Ca <= 'Z') {
                Ca = (char)(Ca - 'A' + 'a');
            }
            if (Cb >= 'A' && Cb <= 'Z') {
                Cb = (char)(Cb - 'A' + 'a');
            }
            if (Ca != Cb) {
                break;
            }
        }
        if (i == 2 && (gCwd[2] == 0 || gCwd[2] == '/' || n == 2)) {
            if (gCwd[2] == 0 || gCwd[2] == '/') {
                return 1;
            }
        }
    }
    /* 亦匹配卷名路径 TOYOS:... */
    if (FileSystemVolInfo(Idx, Name, (int)sizeof(Name), 0, 0, 0) == 0 && Name[0]) {
        int L = 0;
        while (Name[L]) {
            NamePath[L] = Name[L];
            L++;
        }
        NamePath[L++] = ':';
        NamePath[L] = 0;
        if (PathEqIgnoreCase(gCwd, NamePath)) {
            return 1;
        }
        for (i = 0; NamePath[i] && gCwd[i]; i++) {
            char Ca = NamePath[i];
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
        if (NamePath[i] == 0 && (gCwd[i] == 0 || gCwd[i] == '/')) {
            return 1;
        }
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


void GotoPath(const char *Path) {
    CopyStr(gCwd, sizeof(gCwd), Path ? Path : "");
    gMode = FILES_MODE_LIST;
    SetStatus("");
    (void)ReloadList();
    Paint();
}

int SideHitIndex(UINT32 X, UINT32 Y) {
    int Row;

    if (gSideW == 0 || gSideLineH == 0) {
        return -1;
    }
    if (X < gSideX || X >= gSideX + gSideW - 3) {
        return -1;
    }
    if (Y < gSideRow0) {
        return -1;
    }
    Row = (int)((Y - gSideRow0) / gSideLineH);
    if (Row < 0 || Row >= gPlaceCount) {
        return -1;
    }
    return Row;
}

int ReloadListEx(int WantPlaces, int WantPreview) {
    int Err;

    if (WantPlaces) {
        RebuildPlaces();
    }
    gCount = 0;
    gSelected = 0;
    gScroll = 0;
    gHoverIdx = -1;
    Err = FileSystemListEntries(gCwd[0] ? gCwd : "", gEnts, FAT_LIST_MAX, &gCount);
    if (Err != FAT_OK) {
        gCount = 0;
        SetStatus(FatStrError(Err));
        SyncSideSel();
        gPrevKind = PREV_EMPTY;
        gViewLen = 0;
        gViewTitle[0] = 0;
        return Err;
    }
    SyncSideSel();
    if (WantPreview) {
        UpdatePreview();
    } else {
        /* 开窗：目录/ELF 元数据即可，跳过读盘预览 */
        gViewLen = 0;
        gViewTitle[0] = 0;
        gPrevKind = PREV_EMPTY;
        if (gCount > 0 && gSelected >= 0 && gSelected < gCount) {
            FAT_DIRECTORY_ENTRY *E = &gEnts[gSelected];
            CopyStr(gViewTitle, sizeof(gViewTitle), E->Name);
            if (E->Attr & FAT_ATTR_DIR) {
                gPrevKind = PREV_DIR;
            } else if (EndsWithElf(E->Name)) {
                gPrevKind = PREV_ELF;
            } else {
                gPrevKind = PREV_NONE;
            }
        }
    }
    return FAT_OK;
}

int ReloadList(void) {
    return ReloadListEx(1, 1);
}

int IsMostlyText(const char *Buf, UINTN Len) {
    UINTN i;
    UINTN Ok = 0;

    if (Len == 0) {
        return 1;
    }
    for (i = 0; i < Len; i++) {
        unsigned char C = (unsigned char)Buf[i];
        if (C == '\n' || C == '\r' || C == '\t' || (C >= 32 && C < 127)) {
            Ok++;
        }
    }
    return Ok * 10 >= Len * 8;
}

void UpdatePreview(void) {
    FAT_DIRECTORY_ENTRY *E;
    char Path[FILES_PATH_MAX];

    gViewLen = 0;
    gViewTitle[0] = 0;
    gPrevKind = PREV_NONE;

    if (gCount <= 0 || gSelected < 0 || gSelected >= gCount) {
        gPrevKind = PREV_EMPTY;
        return;
    }
    E = &gEnts[gSelected];
    CopyStr(gViewTitle, sizeof(gViewTitle), E->Name);

    if (E->Attr & FAT_ATTR_DIR) {
        gPrevKind = PREV_DIR;
        return;
    }
    if (EndsWithElf(E->Name)) {
        gPrevKind = PREV_ELF;
        return;
    }
    if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
        gPrevKind = PREV_ERR;
        return;
    }
    if (FileSystemReadFile(Path, gView, sizeof(gView) - 1, &gViewLen) != FAT_OK) {
        gViewLen = 0;
        gPrevKind = PREV_ERR;
        return;
    }
    gView[gViewLen] = 0;
    if (IsMostlyText(gView, gViewLen)) {
        gPrevKind = PREV_TEXT;
    } else {
        gPrevKind = PREV_BIN;
    }
}
