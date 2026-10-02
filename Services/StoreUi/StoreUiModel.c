/*
 * StoreUiModel.c — 分类过滤、已装缓存、选中项、仓库源文件
 * 核心：StoreUi.c
 */
#include "StoreUiPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Hal.h"

const char *StoreBtnLabel(int I) {
    switch (I) {
    case 0:
        return LocStr(MSG_STORE_INSTALL);
    case 1:
        return LocStr(MSG_STORE_REMOVE);
    case 2:
        return LocStr(MSG_STORE_SYNC);
    case 3:
        return LocStr(MSG_STORE_REPO);
    default:
        return "?";
    }
}

const char *StoreCatLabel(int C) {
    switch (C) {
    case STORE_CAT_ALL:
        return LocStr(MSG_STORE_CAT_ALL);
    case STORE_CAT_APP:
        return LocStr(MSG_STORE_CAT_APP);
    case STORE_CAT_FONT:
        return LocStr(MSG_STORE_CAT_FONT);
    case STORE_CAT_ASSET:
        return LocStr(MSG_STORE_CAT_ASSET);
    case STORE_CAT_INSTALLED:
        return LocStr(MSG_STORE_CAT_INST);
    default:
        return "?";
    }
}

static void RefreshInstCache(void) {
    STORE_ENTRY *Tab = StoreScratchTab();

    StoreFillInstalledFlags(Tab, gCatalogN, gInstCache);
    {
        int i;
        for (i = gCatalogN; i < STORE_ENTRIES_MAX; i++) {
            gInstCache[i] = 0;
        }
    }
}

int CachedInstalled(int CatalogIdx) {
    if (CatalogIdx < 0 || CatalogIdx >= gCatalogN || CatalogIdx >= STORE_ENTRIES_MAX) {
        return 0;
    }
    return gInstCache[CatalogIdx];
}

void StoreSetStatus(const char *S) {
    int i;

    for (i = 0; i < (int)sizeof(gStoreUiStatus) - 1 && S && S[i]; i++) {
        gStoreUiStatus[i] = S[i];
    }
    gStoreUiStatus[i] = 0;
}

static int EntryMatches(const STORE_ENTRY *E, int Cat, int CatalogIdx) {
    if (!E) {
        return 0;
    }
    switch (Cat) {
    case STORE_CAT_ALL:
        return 1;
    case STORE_CAT_APP:
        return StrEq(E->Type, "app");
    case STORE_CAT_FONT:
        return StrEq(E->Type, "font");
    case STORE_CAT_ASSET:
        return StrEq(E->Type, "asset");
    case STORE_CAT_INSTALLED:
        return CachedInstalled(CatalogIdx);
    default:
        return 0;
    }
}

void RebuildFilter(void) {
    STORE_ENTRY *Tab = StoreScratchTab();
    int i;
    char KeepId[STORE_ID_MAX];
    int j;

    KeepId[0] = 0;
    if (gSel >= 0 && gSel < gFiltCount && gMap[gSel] >= 0 &&
        gMap[gSel] < gCatalogN) {
        for (j = 0; j < STORE_ID_MAX - 1 && Tab[gMap[gSel]].Id[j]; j++) {
            KeepId[j] = Tab[gMap[gSel]].Id[j];
        }
        KeepId[j] = 0;
    }

    gFiltCount = 0;
    for (i = 0; i < gCatalogN && gFiltCount < STORE_MAP_MAX; i++) {
        if (EntryMatches(&Tab[i], gStoreUiCat, i)) {
            gMap[gFiltCount++] = i;
        }
    }
    gSel = 0;
    if (KeepId[0]) {
        for (i = 0; i < gFiltCount; i++) {
            if (StrEq(Tab[gMap[i]].Id, KeepId)) {
                gSel = i;
                break;
            }
        }
    }
    if (gFiltCount == 0) {
        gSel = 0;
    } else if (gSel >= gFiltCount) {
        gSel = gFiltCount - 1;
    }
}

void Reload(void) {
    STORE_ENTRY *Tab = StoreScratchTab();
    int N = 0;

    gCatalogN = 0;
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &N) >= 0 && N > 0) {
        gCatalogN = N;
    }
    RefreshInstCache();
    RebuildFilter();
}

STORE_ENTRY *SelectedEntry(void) {
    STORE_ENTRY *Tab = StoreScratchTab();

    if (gFiltCount <= 0 || gSel < 0 || gSel >= gFiltCount) {
        return 0;
    }
    return &Tab[gMap[gSel]];
}

void StoreBtnGeom(UINT32 ListX, UINT32 ListW) {
    UINT32 i;
    UINT32 MaxLabel = 0;
    UINT32 Tw;
    UINT32 Need;
    UINT32 Total;
    UINT32 Pad = 24u;

    for (i = 0; i < (UINT32)STORE_BTN_N; i++) {
        Tw = FontStringWidth(StoreBtnLabel(i));
        if (Tw > MaxLabel) {
            MaxLabel = Tw;
        }
    }
    Need = MaxLabel + Pad;
    if (Need < 80u) {
        Need = 80u;
    }
    Total = Need * (UINT32)STORE_BTN_N + STORE_BTN_GAP * (UINT32)(STORE_BTN_N - 1);
    if (Total + 16u > ListW) {
        gBtnW = (ListW > 16u + STORE_BTN_GAP * (UINT32)(STORE_BTN_N - 1))
                    ? (ListW - 16u - STORE_BTN_GAP * (UINT32)(STORE_BTN_N - 1)) /
                          (UINT32)STORE_BTN_N
                    : 56u;
    } else {
        gBtnW = Need;
    }
    gBtnX0 = ListX + 8;
}

void ClampScroll(void) {
    if (gStoreUiListVisible < 1) {
        gStoreUiListVisible = 1;
    }
    if (gSel < gStoreUiScroll) {
        gStoreUiScroll = gSel;
    }
    if (gSel >= gStoreUiScroll + gStoreUiListVisible) {
        gStoreUiScroll = gSel - gStoreUiListVisible + 1;
    }
    if (gStoreUiScroll < 0) {
        gStoreUiScroll = 0;
    }
}

void StoreUiFormatRepo(char *Out, int OutMax) {
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];
    int i = 0;
    int j;
    UINT32 P;
    char Dig[8];
    int Dn;

    if (!Out || OutMax <= 0) {
        return;
    }
    StoreRepoGet(&Ip, &Port);
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    Out[i++] = 'r';
    if (i < OutMax - 1) Out[i++] = 'e';
    if (i < OutMax - 1) Out[i++] = 'p';
    if (i < OutMax - 1) Out[i++] = 'o';
    if (i < OutMax - 1) Out[i++] = ':';
    if (i < OutMax - 1) Out[i++] = ' ';
    for (j = 0; IpBuf[j] && i < OutMax - 1; j++) {
        Out[i++] = IpBuf[j];
    }
    if (i < OutMax - 1) {
        Out[i++] = ':';
    }
    P = (UINT32)Port;
    Dn = 0;
    if (P == 0) {
        Dig[Dn++] = '0';
    } else {
        while (P > 0 && Dn < (int)sizeof(Dig)) {
            Dig[Dn++] = (char)('0' + (P % 10u));
            P /= 10u;
        }
    }
    while (Dn > 0 && i < OutMax - 1) {
        Out[i++] = Dig[--Dn];
    }
    Out[i] = 0;
}

void StoreUiApplyRepoFile(void) {
    UINT8 Buf[64];
    UINTN Size = 0;
    char Line[48];
    UINTN i = 0;
    UINTN j = 0;
    int Err;

    Err = FileSystemReadFile(STORE_REPO_PATH, Buf, sizeof(Buf) - 1u, &Size);
    if (Err != FAT_OK || Size == 0) {
        return;
    }
    Buf[Size] = 0;
    while (i < Size && (Buf[i] == ' ' || Buf[i] == '\t')) {
        i++;
    }
    while (i < Size && Buf[i] != '\n' && Buf[i] != '\r' && j + 1 < sizeof(Line)) {
        Line[j++] = (char)Buf[i++];
    }
    while (j > 0 && (Line[j - 1] == ' ' || Line[j - 1] == '\t')) {
        j--;
    }
    Line[j] = 0;
    if (Line[0] == 0 || Line[0] == '#') {
        return;
    }
    if (StoreRepoSet(Line) == 0) {
        StoreSetStatus("repo updated");
    }
}

int StoreUiWriteRepoFile(void) {
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];
    char Line[40];
    int i = 0;
    int j;
    UINT32 P;
    char Dig[8];
    int Dn;
    int Err;

    Err = FileSystemMakeDirectory(STORE_DIR);
    if (Err != FAT_OK && Err != FAT_ERR_EXIST) {
        return Err;
    }
    StoreRepoGet(&Ip, &Port);
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    for (j = 0; IpBuf[j] && i < (int)sizeof(Line) - 1; j++) {
        Line[i++] = IpBuf[j];
    }
    if (i < (int)sizeof(Line) - 1) {
        Line[i++] = ':';
    }
    P = (UINT32)Port;
    Dn = 0;
    if (P == 0) {
        Dig[Dn++] = '0';
    } else {
        while (P > 0 && Dn < (int)sizeof(Dig)) {
            Dig[Dn++] = (char)('0' + (P % 10u));
            P /= 10u;
        }
    }
    while (Dn > 0 && i < (int)sizeof(Line) - 1) {
        Line[i++] = Dig[--Dn];
    }
    Line[i++] = '\n';
    Err = FileSystemWriteFile(STORE_REPO_PATH, (const UINT8 *)Line, (UINTN)i);
    return Err == FAT_OK ? 0 : Err;
}
