/*
 * FilesUiPreview.c — 跳转、重载列表与预览
 * 核心：FilesUi.c
 */
#include "FilesUiPrivate.h"
#include "Fat.h"

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
