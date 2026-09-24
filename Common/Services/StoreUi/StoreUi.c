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

const char *const gBtnLabel[STORE_BTN_N] = {
    "Install", "Remove", "Sync"
};

/* PR-GUI-migrate-store：底栏三钮 ASYNC，Fn 仍走 StoreJobEnqueue */
UI_BUTTON_ACTION gStoreAct[STORE_BTN_N];

static void ActInstall(void *Ctx) { (void)Ctx; StoreUiDoButton(0); }
static void ActRemove(void *Ctx)  { (void)Ctx; StoreUiDoButton(1); }
static void ActSync(void *Ctx)    { (void)Ctx; StoreUiDoButton(2); }

void StoreUiActInit(void) {
    static UI_ACTION_FN const Fns[STORE_BTN_N] = {
        ActInstall, ActRemove, ActSync
    };
    int i;

    for (i = 0; i < STORE_BTN_N; i++) {
        gStoreAct[i].Button.X = 0;
        gStoreAct[i].Button.Y = 0;
        gStoreAct[i].Button.W = 0;
        gStoreAct[i].Button.H = STORE_BTN_H;
        gStoreAct[i].Button.Text = gBtnLabel[i];
        gStoreAct[i].Button.Enabled = 1;
        gStoreAct[i].Button.Visible = 1;
        gStoreAct[i].Button.m_State = UI_BUTTON_STATE_NORMAL;
        gStoreAct[i].Kind = UI_ACTION_ASYNC;
        gStoreAct[i].Fn = Fns[i];
        gStoreAct[i].Ctx = 0;
    }
}

int StoreUiActDispatch(int Btn, int Pressed, int Hit) {
    int i;

    if (Btn < 0 || Btn >= STORE_BTN_N) {
        return 0;
    }
    for (i = 0; i < STORE_BTN_N; i++) {
        gStoreAct[i].Button.X = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
        gStoreAct[i].Button.Y = gBtnY;
        gStoreAct[i].Button.W = gBtnW;
        gStoreAct[i].Button.H = STORE_BTN_H;
        gStoreAct[i].Button.Text = gBtnLabel[i];
        gStoreAct[i].Button.Visible = 1;
        gStoreAct[i].Button.Enabled = 1;
    }
    return UiActionDispatch(&gStoreAct[Btn], Pressed, Hit);
}

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
    StoreUiActInit();
    Reload();
    StoreSetStatus(gFiltCount > 0 ? "select / Install|Remove|Sync" : "no catalog");
    StorePaintList();
    DebugWrite("store-ui: three-pane open\n");
}

int StoreUiIsBusy(void) {
    return StoreJobIsBusy();
}

void StoreUiOnEscape(void) {
    if (!StoreUiIsFocused()) {
        return;
    }
    if (!StoreJobIsBusy()) {
        return;
    }
    if (StoreJobCancel() == 0) {
        StoreSetStatus("cancelling...");
        StoreUiRepaint();
    }
}
