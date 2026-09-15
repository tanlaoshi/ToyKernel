/*
 * FilesUiNav.c — Files 路径/书签/预览（PR-S-filesui-split-2）
 *
 * 从 FilesUi.c 迁出 Nav*；只搬家、不改逻辑。
 */
#include "FilesUiPriv.h"
#include "Fat.h"

/* 当前 cwd 是否落在该书签（精确或卷内子路径前缀） */
int BookmarkMatches(int Idx) {
    const char *P;
    int n;
    int i;

    if (Idx < 0 || Idx >= FILES_BOOKMARK_COUNT) {
        return 0;
    }
    P = gBookmarks[Idx].Path;
    if (PathEqIgnoreCase(gCwd, P)) {
        return 1;
    }
    /* "TOYOS:" 匹配空 cwd（默认卷根） */
    n = 0;
    while (P[n]) {
        n++;
    }
    if (n > 0 && P[n - 1] == ':' && gCwd[0] == 0 &&
        FilesUiStrEqIgnoreCase(gBookmarks[Idx].Label, "TOYOS:")) {
        return 1;
    }
    /* Apps/Assets：cwd 为 TOYOS:Apps/... */
    if (n > 0 && P[n - 1] != ':') {
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
        if (P[i] == 0 && (gCwd[i] == 0 || gCwd[i] == '/')) {
            return 1;
        }
    }
    return 0;
}

void SyncSideSel(void) {
    int i;
    gSideSel = -1;
    for (i = 0; i < FILES_BOOKMARK_COUNT; i++) {
        if (BookmarkMatches(i)) {
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
    if (Row < 0 || Row >= FILES_BOOKMARK_COUNT) {
        return -1;
    }
    return Row;
}

int ReloadList(void) {
    int Err;

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
    UpdatePreview();
    return FAT_OK;
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

