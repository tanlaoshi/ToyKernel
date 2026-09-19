/*
 * StoreUi.c — 商店三分栏核心（全局、开窗）
 * 过滤：StoreUiModel.c；绘制：StoreUiPaint.c；输入：StoreUiInput.c。
 */
#include "StoreUiPrivate.h"

int gStoreUiCat;
int gSel;
int gStoreUiScroll;
int gFiltCount;
int gMap[STORE_MAP_MAX];
int gInstCache[STORE_ENTRIES_MAX];
int gCatalogN;
char gStoreUiStatus[80];

UINT32 gStoreUiSideX, gStoreUiSideW, gStoreUiSideRow0, gStoreUiSideLineH;
UINT32 gListX, gStoreUiListTop, gStoreUiListRowW, gStoreUiListLineH;
int gStoreUiListVisible;
UINT32 gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH;
int gStoreUiSbVisible;
UINT32 gStoreUiPrevX, gStoreUiPrevW;
UINT32 gBtnY, gBtnW, gBtnX0;
int gHoverSide = -1;
int gHoverRow = -1;
int gHoverBtn = -1;
int gPressBtn = -1;
/* 0=无 1=install 2=remove 3=sync；抬起只入队，GuiPollMouse 末尾 Pump */
int gJobPending;
int gJobBusy;
char gJobId[STORE_ID_MAX];

const char *const gBtnLabel[STORE_BTN_N] = {
    "Install", "Remove", "Sync"
};

int StoreUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_STORE;
}

void StoreUiPaintFocused(void) {
    if (!StoreUiIsFocused()) {
        return;
    }
    StorePaintList();
}

void StoreUiRepaint(void) {
    if (!StoreUiIsFocused()) {
        return;
    }
    StorePaintList();
    GuiBackupFocusWindow();
}

void StoreUiOpen(void) {
    gStoreUiCat = STORE_CAT_ALL;
    gSel = 0;
    gStoreUiScroll = 0;
    gHoverSide = -1;
    gHoverRow = -1;
    gHoverBtn = -1;
    gPressBtn = -1;
    Reload();
    StoreSetStatus(gFiltCount > 0 ? "select / Install|Remove|Sync" : "no catalog");
    StorePaintList();
    DebugWrite("store-ui: three-pane open\n");
}

int StoreUiIsBusy(void) {
    return (gJobBusy || gJobPending != 0) ? 1 : 0;
}
