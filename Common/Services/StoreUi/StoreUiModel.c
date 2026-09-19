/*
 * StoreUiModel.c — 分类过滤、已装缓存、选中项
 * 核心：StoreUi.c
 */
#include "StoreUiPriv.h"

const char *StoreCatLabel(int C) {
    switch (C) {
    case STORE_CAT_ALL:       return "All";
    case STORE_CAT_APP:       return "Apps";
    case STORE_CAT_FONT:      return "Fonts";
    case STORE_CAT_ASSET:     return "Assets";
    case STORE_CAT_INSTALLED: return "Installed";
    default:                  return "?";
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
        Tw = FontStringWidth(gBtnLabel[i]);
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
