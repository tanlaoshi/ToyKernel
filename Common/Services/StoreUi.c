/*
 * StoreUi.c — 商店三分栏（左类型 / 中条目+底栏按钮 / 右详情）
 *
 * 悬停重绘只用 gInstCache，禁止每帧 StoreIsInstalled 查盘。
 */
#include "StoreUi.h"
#include "Store.h"
#include "Desktop.h"
#include "Gui.h"
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Debug.h"
#include "UI.h"

#define STORE_SIDE_W    128u
#define STORE_SIDE_BG   0x00A0A8B0u
#define STORE_PREV_BG   0x00D8D8E0u
#define STORE_SB_W      12u
#define STORE_BTN_H     28u
#define STORE_BTN_GAP   10u
#define STORE_BTN_N     3
#define STORE_MAP_MAX   STORE_ENTRIES_MAX

typedef enum {
    STORE_CAT_ALL = 0,
    STORE_CAT_APP,
    STORE_CAT_FONT,
    STORE_CAT_ASSET,
    STORE_CAT_INSTALLED,
    STORE_CAT_COUNT
} STORE_CAT;

static int gCat;
static int gSel;
static int gScroll;
static int gFiltCount;
static int gMap[STORE_MAP_MAX];
static int gInstCache[STORE_ENTRIES_MAX];
static int gCatalogN;
static char gStatus[80];

static UINT32 gSideX, gSideW, gSideRow0, gSideLineH;
static UINT32 gListX, gListTop, gListRowW, gListLineH;
static int gListVisible;
static UINT32 gSbX, gSbY, gSbW, gSbH;
static int gSbVisible;
static UINT32 gPrevX, gPrevW;
static UINT32 gBtnY, gBtnW, gBtnX0;
static int gHoverSide = -1;
static int gHoverRow = -1;
static int gHoverBtn = -1;
static int gPressBtn = -1;
/* 0=无 1=install 2=remove 3=sync；抬起只入队，GuiPollMouse 末尾 Pump */
static int gJobPending;
static int gJobBusy;
static char gJobId[STORE_ID_MAX];

static const char *const gBtnLabel[STORE_BTN_N] = {
    "Install", "Remove", "Sync"
};

static const char *CatLabel(int C) {
    switch (C) {
    case STORE_CAT_ALL:       return "All";
    case STORE_CAT_APP:       return "Apps";
    case STORE_CAT_FONT:      return "Fonts";
    case STORE_CAT_ASSET:     return "Assets";
    case STORE_CAT_INSTALLED: return "Installed";
    default:                  return "?";
    }
}

static int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
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

static int CachedInstalled(int CatalogIdx) {
    if (CatalogIdx < 0 || CatalogIdx >= gCatalogN || CatalogIdx >= STORE_ENTRIES_MAX) {
        return 0;
    }
    return gInstCache[CatalogIdx];
}

static void SetStatus(const char *S) {
    int i;

    for (i = 0; i < (int)sizeof(gStatus) - 1 && S && S[i]; i++) {
        gStatus[i] = S[i];
    }
    gStatus[i] = 0;
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

static void RebuildFilter(void) {
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
        if (EntryMatches(&Tab[i], gCat, i)) {
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

static void Reload(void) {
    STORE_ENTRY *Tab = StoreScratchTab();
    int N = 0;

    gCatalogN = 0;
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &N) >= 0 && N > 0) {
        gCatalogN = N;
    }
    RefreshInstCache();
    RebuildFilter();
}

static STORE_ENTRY *SelectedEntry(void) {
    STORE_ENTRY *Tab = StoreScratchTab();

    if (gFiltCount <= 0 || gSel < 0 || gSel >= gFiltCount) {
        return 0;
    }
    return &Tab[gMap[gSel]];
}

static void StoreBtnGeom(UINT32 ListX, UINT32 ListW) {
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

static void ClampScroll(void) {
    if (gListVisible < 1) {
        gListVisible = 1;
    }
    if (gSel < gScroll) {
        gScroll = gSel;
    }
    if (gSel >= gScroll + gListVisible) {
        gScroll = gSel - gListVisible + 1;
    }
    if (gScroll < 0) {
        gScroll = 0;
    }
}

static void DrawButtons(void) {
    int i;
    UINT32 Bx;

    for (i = 0; i < STORE_BTN_N; i++) {
        Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
        {
            UINT32 Face = (gHoverBtn == i) ? ThemeControlAccent() : ThemeControlFace();
            UINT32 Fg = (gHoverBtn == i) ? COLOR_WHITE : COLOR_BLACK;
            UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, gBtnLabel[i], Fg, Face,
                           gHoverBtn == i, gPressBtn == i);
        }
    }
}

static void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Ty;
    UINT32 MaxY;
    STORE_ENTRY *E;
    int Inst;
    char Line[80];
    int MapIdx;

    LineH = FontAdvanceY();
    if (LineH < 14) {
        LineH = 14;
    }
    HalVideoFillRect(X, Y, W, H, STORE_PREV_BG);
    if (W > 3) {
        HalVideoFillRect(X, Y, 3, H, COLOR_DARK_GRAY);
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    HalVideoDrawStringAt(X + 10, Ty, "Detail", COLOR_BLACK);
    Ty += LineH + 4;

    E = SelectedEntry();
    MapIdx = (gSel >= 0 && gSel < gFiltCount) ? gMap[gSel] : -1;
    if (!E) {
        HalVideoDrawStringAt(X + 10, Ty, "(no selection)", COLOR_DARK_GRAY);
        return;
    }
    Inst = CachedInstalled(MapIdx);
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, E->Title[0] ? E->Title : E->Id, COLOR_BLACK);
        Ty += LineH + 2;
    }
    if (Ty + LineH < MaxY) {
        Line[0] = 'i'; Line[1] = 'd'; Line[2] = ':'; Line[3] = ' ';
        {
            int k = 4;
            const char *P = E->Id;
            while (*P && k < 70) {
                Line[k++] = *P++;
            }
            Line[k] = 0;
        }
        HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        Line[0] = 't'; Line[1] = 'y'; Line[2] = 'p'; Line[3] = 'e';
        Line[4] = ':'; Line[5] = ' ';
        {
            int k = 6;
            const char *P = E->Type;
            while (*P && k < 70) {
                Line[k++] = *P++;
            }
            Line[k] = 0;
        }
        HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
        Ty += LineH;
    }
    if (E->Arch[0] && Ty + LineH < MaxY) {
        Line[0] = 'a'; Line[1] = 'r'; Line[2] = 'c'; Line[3] = 'h';
        Line[4] = ':'; Line[5] = ' ';
        {
            int k = 6;
            const char *P = E->Arch;
            while (*P && k < 70) {
                Line[k++] = *P++;
            }
            Line[k] = 0;
        }
        HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
        Ty += LineH;
    }
    if (E->File[0] && Ty + LineH < MaxY) {
        Line[0] = 'f'; Line[1] = 'i'; Line[2] = 'l'; Line[3] = 'e';
        Line[4] = ':'; Line[5] = ' ';
        {
            int k = 6;
            const char *P = E->File;
            while (*P && k < 70) {
                Line[k++] = *P++;
            }
            Line[k] = 0;
        }
        HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
        Ty += LineH;
    }
    if (E->Depends[0] && Ty + LineH < MaxY) {
        Line[0] = 'd'; Line[1] = 'e'; Line[2] = 'p'; Line[3] = ':';
        Line[4] = ' ';
        {
            int k = 5;
            const char *P = E->Depends;
            while (*P && k < 70) {
                Line[k++] = *P++;
            }
            Line[k] = 0;
        }
        HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, Inst ? "status: installed" : "status: not installed",
                             Inst ? COLOR_BLUE : COLOR_DARK_GRAY);
    }
}

static void PaintList(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 SideW;
    UINT32 ContentX, ContentW;
    UINT32 ListW;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 FootH;
    UINT32 ListBottom;
    int i;
    STORE_ENTRY *Tab = StoreScratchTab();
    char Row[72];

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    SideW = 0;
    gSideW = 0;
    if (Cw > STORE_SIDE_W + 160u) {
        SideW = STORE_SIDE_W;
    }
    ContentX = Cx + SideW;
    ContentW = Cw - SideW;

    if (SideW > 0) {
        gSideX = Cx;
        gSideW = SideW;
        gSideLineH = LineH;
        gSideRow0 = Cy + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, STORE_SIDE_BG);
        if (SideW > 3) {
            HalVideoFillRect(Cx + SideW - 3, Cy, 3, Ch, COLOR_DARK_GRAY);
        }
        HalVideoDrawStringAt(Cx + 8, Cy + 8, LocStr(MSG_APP_STORE), COLOR_BLACK);
        for (i = 0; i < STORE_CAT_COUNT; i++) {
            UiDrawListRow(Cx + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          CatLabel(i), i == gCat, i == gHoverSide);
        }
    }

    gPrevW = 0;
    if (ContentW > 380u) {
        gPrevW = ContentW * 2u / 5u;
        if (gPrevW < 180u) {
            gPrevW = 180u;
        }
        if (gPrevW + 200u > ContentW) {
            gPrevW = ContentW > 200u ? ContentW - 200u : 0;
        }
    }

    ListW = ContentW - gPrevW;
    gListX = ContentX;
    FootH = STORE_BTN_H + LineH + 16u;
    if (FootH + LineH * 4 > Ch) {
        FootH = STORE_BTN_H + 12u;
    }
    ListBottom = Cy + Ch - FootH;
    gListTop = Cy + 8 + LineH + 4;
    gListLineH = LineH;
    gListVisible = 1;
    if (ListBottom > gListTop + LineH) {
        gListVisible = (int)((ListBottom - gListTop) / LineH);
    }
    if (gListVisible < 1) {
        gListVisible = 1;
    }
    ClampScroll();

    gSbVisible = (gFiltCount > gListVisible) ? 1 : 0;
    gSbW = STORE_SB_W;
    gSbH = (UINT32)gListVisible * LineH;
    if (gSbH + gListTop > ListBottom) {
        gSbH = (ListBottom > gListTop) ? (ListBottom - gListTop) : 0;
    }
    gSbX = (ListW > STORE_SB_W + 8) ? (ContentX + ListW - STORE_SB_W - 4)
                                    : (ContentX + 4);
    gSbY = gListTop;
    gListRowW = ListW > 8 ? ListW - 8 : ListW;
    if (gSbVisible && gListRowW > STORE_SB_W + 8) {
        gListRowW -= (STORE_SB_W + 4);
    }

    HalVideoDrawStringAt(ContentX + 8, Cy + 8, CatLabel(gCat), COLOR_BLACK);

    RowY = gListTop;
    for (i = 0; i < gListVisible && gScroll + i < gFiltCount; i++) {
        int Fi = gScroll + i;
        int Ci = gMap[Fi];
        int Inst = CachedInstalled(Ci);
        int k = 0;
        const char *P;

        Row[k++] = Inst ? '*' : ' ';
        Row[k++] = ' ';
        P = Tab[Ci].Id;
        while (*P && k < 28) {
            Row[k++] = *P++;
        }
        Row[k++] = ' ';
        P = Tab[Ci].Title;
        while (*P && k < 70) {
            Row[k++] = *P++;
        }
        Row[k] = 0;
        UiDrawListRow(ContentX + 4, RowY, gListRowW, LineH, Row,
                      Fi == gSel, Fi == gHoverRow);
        RowY += LineH;
    }
    if (gSbVisible && gSbH > 0) {
        UiDrawScrollBar(gSbX, gSbY, gSbW, gSbH, gScroll, gListVisible, gFiltCount);
    }
    if (gFiltCount == 0) {
        HalVideoDrawStringAt(ContentX + 12, gListTop + 4, "(empty)", COLOR_DARK_GRAY);
    }

    /* 第 2 分栏下方：三钮均分中栏宽度 */
    gBtnY = ListBottom + 4;
    StoreBtnGeom(ContentX, ListW);
    DrawButtons();
    if (gStatus[0]) {
        HalVideoDrawStringAt(ContentX + 8, Cy + Ch - LineH - 2, gStatus, COLOR_DARK_GRAY);
    }

    if (gPrevW > 0) {
        gPrevX = ContentX + ListW;
        DrawDetail(gPrevX, Cy, gPrevW, Ch);
    } else {
        gPrevX = ContentX + ListW;
        gPrevW = 0;
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

int StoreUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_STORE;
}

void StoreUiPaintFocused(void) {
    if (!StoreUiIsFocused()) {
        return;
    }
    PaintList();
}

void StoreUiRepaint(void) {
    if (!StoreUiIsFocused()) {
        return;
    }
    PaintList();
    GuiBackupFocusWindow();
}

void StoreUiOpen(void) {
    gCat = STORE_CAT_ALL;
    gSel = 0;
    gScroll = 0;
    gHoverSide = -1;
    gHoverRow = -1;
    gHoverBtn = -1;
    gPressBtn = -1;
    Reload();
    SetStatus(gFiltCount > 0 ? "select / Install|Remove|Sync" : "no catalog");
    PaintList();
    DebugWrite("store-ui: three-pane open\n");
}

static void DoBtn(int Btn) {
    STORE_ENTRY *E;
    int i;

    if (gJobPending || gJobBusy) {
        SetStatus("busy...");
        StoreUiRepaint();
        return;
    }
    if (Btn == 2) {
        gJobPending = 3;
        gJobId[0] = 0;
        SetStatus("syncing...");
        StoreUiRepaint();
        return;
    }
    E = SelectedEntry();
    if (!E) {
        SetStatus("no selection");
        StoreUiRepaint();
        return;
    }
    for (i = 0; E->Id[i] && i < STORE_ID_MAX - 1; i++) {
        gJobId[i] = E->Id[i];
    }
    gJobId[i] = 0;
    if (Btn == 0) {
        gJobPending = 1;
        SetStatus("installing...");
    } else {
        gJobPending = 2;
        SetStatus("removing...");
    }
    StoreUiRepaint();
}

void StoreUiPump(void) {
    int Job;
    int Err;

    if (gJobBusy || gJobPending == 0) {
        return;
    }
    Job = gJobPending;
    gJobPending = 0;
    gJobBusy = 1;

    if (Job == 1) {
        Err = StoreComboInstall(gJobId);
        SetStatus(Err == 0 ? "installed" : "install fail");
    } else if (Job == 2) {
        Err = StoreComboRemove(gJobId);
        SetStatus(Err == 0 ? "removed" : "remove fail");
    } else {
        Err = StoreSyncCatalog();
        SetStatus(Err == 0 ? "sync ok" : "sync fail (need repo)");
    }
    GuiPollMouseMotion();
    Reload();
    GuiPollMouseMotion();
    DesktopNotifyAppsChanged();
    if (StoreUiIsFocused()) {
        StoreUiRepaint();
    }
    gJobBusy = 0;
}

void StoreUiOnClick(UINT32 X, UINT32 Y) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    int First;
    int Row;
    int i;
    UINT32 Bx;

    if (!StoreUiIsFocused()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        return;
    }

    if (gSideW > 0 && X < Cx + gSideW) {
        if (Y >= gSideRow0) {
            Row = (int)((Y - gSideRow0) / gSideLineH);
            if (Row >= 0 && Row < STORE_CAT_COUNT) {
                gCat = Row;
                gScroll = 0;
                RebuildFilter();
                SetStatus(CatLabel(gCat));
                StoreUiRepaint();
            }
        }
        return;
    }

    if (Y >= gBtnY && Y < gBtnY + STORE_BTN_H) {
        for (i = 0; i < STORE_BTN_N; i++) {
            Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
            if (X >= Bx && X < Bx + gBtnW) {
                DoBtn(i);
                return;
            }
        }
    }

    if (gSbVisible &&
        UiScrollBarHit(gSbX, gSbY, gSbW, gSbH, gScroll, gListVisible, gFiltCount,
                       X, Y, &First)) {
        gScroll = First;
        StoreUiRepaint();
        return;
    }

    if (gPrevW > 0 && X >= gPrevX) {
        return;
    }
    if (Y >= gListTop && Y < gBtnY && gFiltCount > 0) {
        Row = UiListRowFromY(gListTop, gListLineH, gListVisible, Y);
        if (Row >= 0) {
            int Fi = gScroll + Row;
            if (Fi >= 0 && Fi < gFiltCount) {
                gSel = Fi;
                StoreUiRepaint();
            }
        }
    }
}

int StoreUiIsBusy(void) {
    return (gJobBusy || gJobPending != 0) ? 1 : 0;
}

void StoreUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    int Side = -1;
    int Row = -1;
    int Btn = -1;
    int Need = 0;
    int FireBtn = -1;
    int FireSide = -1;
    int FireRow = -1;
    UINT32 Bx;
    int i;
    static UINT8 sPrevBtn;

    /* 装卸中只挪光标（CursorMove 已做）；禁止整窗重绘抢 Present */
    if (gJobBusy) {
        sPrevBtn = Buttons;
        return;
    }

    if (!StoreUiIsFocused()) {
        if (gHoverSide >= 0 || gHoverRow >= 0 || gHoverBtn >= 0 || gPressBtn >= 0) {
            gHoverSide = -1;
            gHoverRow = -1;
            gHoverBtn = -1;
            gPressBtn = -1;
        }
        sPrevBtn = Buttons;
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        sPrevBtn = Buttons;
        return;
    }

    if (gSideW > 0 && X >= Cx && X < Cx + gSideW && Y >= gSideRow0) {
        Side = (int)((Y - gSideRow0) / gSideLineH);
        if (Side < 0 || Side >= STORE_CAT_COUNT) {
            Side = -1;
        }
    } else if (Y >= gBtnY && Y < gBtnY + STORE_BTN_H) {
        for (i = 0; i < STORE_BTN_N; i++) {
            Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
            if (X >= Bx && X < Bx + gBtnW) {
                Btn = i;
                break;
            }
        }
    } else if (!(gPrevW > 0 && X >= gPrevX) && Y >= gListTop && Y < gBtnY &&
               gFiltCount > 0) {
        Row = UiListRowFromY(gListTop, gListLineH, gListVisible, Y);
        if (Row >= 0) {
            int Fi = gScroll + Row;
            Row = (Fi >= 0 && Fi < gFiltCount) ? Fi : -1;
        }
    }

    if (Side != gHoverSide || Row != gHoverRow || Btn != gHoverBtn) {
        gHoverSide = Side;
        gHoverRow = Row;
        gHoverBtn = Btn;
        Need = 1;
    }
    if ((Buttons & 1u) && !(sPrevBtn & 1u)) {
        if (Btn >= 0) {
            gPressBtn = Btn;
            Need = 1;
        } else if (Side >= 0) {
            FireSide = Side;
        } else if (Row >= 0) {
            FireRow = Row;
        }
    } else if ((Buttons & 1u) && gPressBtn >= 0 && Btn != gPressBtn) {
        gPressBtn = -1;
        Need = 1;
    } else if (!(Buttons & 1u) && (sPrevBtn & 1u) && gPressBtn >= 0) {
        if (Btn == gPressBtn) {
            FireBtn = gPressBtn;
        }
        gPressBtn = -1;
        Need = 1;
    }
    sPrevBtn = Buttons;
    if (Need) {
        StoreUiRepaint();
    }
    if (FireSide >= 0) {
        gCat = FireSide;
        gScroll = 0;
        RebuildFilter();
        SetStatus(CatLabel(gCat));
        StoreUiRepaint();
    } else if (FireRow >= 0) {
        gSel = FireRow;
        StoreUiRepaint();
    } else if (FireBtn >= 0) {
        DoBtn(FireBtn);
    }
}
