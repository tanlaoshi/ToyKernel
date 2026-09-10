/*
 * StoreUi.c — PR-G-store-ui：商店窗口（列表 + 安装/卸载）
 */
#include "StoreUi.h"
#include "Store.h"
#include "Gui.h"
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Debug.h"
#include "UI.h"

#define STORE_ROW_H     22u
#define STORE_BTN_H     24u
#define STORE_PAD       8u
#define STORE_BTN_GAP   8u
#define STORE_BTN_N     3u

static int gSel;
static int gCount;
static char gStatus[80];

static const char *const gBtnLabel[STORE_BTN_N] = {
    "Install", "Remove", "Sync"
};

static void SetStatus(const char *S) {
    int i;

    for (i = 0; i < (int)sizeof(gStatus) - 1 && S && S[i]; i++) {
        gStatus[i] = S[i];
    }
    gStatus[i] = 0;
}

static void Reload(void) {
    STORE_ENTRY *Tab = StoreScratchTab();
    int N = 0;
    int Prev = gSel;
    char KeepId[STORE_ID_MAX];
    int i;

    KeepId[0] = 0;
    if (Prev >= 0 && Prev < gCount) {
        for (i = 0; i < STORE_ID_MAX - 1 && Tab[Prev].Id[i]; i++) {
            KeepId[i] = Tab[Prev].Id[i];
        }
        KeepId[i] = 0;
    }

    gCount = 0;
    gSel = 0;
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &N) >= 0 && N > 0) {
        gCount = N;
        if (KeepId[0]) {
            for (i = 0; i < gCount; i++) {
                const char *A = Tab[i].Id;
                const char *B = KeepId;
                while (*A && *A == *B) {
                    A++;
                    B++;
                }
                if (*A == 0 && *B == 0) {
                    gSel = i;
                    break;
                }
            }
        }
        if (gSel >= gCount) {
            gSel = gCount - 1;
        }
    }
}

/* 按字宽算按钮宽，防大字体重叠 */
static void StoreBtnGeom(UINT32 Cx, UINT32 Cw, UINT32 *OutBw, UINT32 *OutX0) {
    UINT32 i;
    UINT32 Need;
    UINT32 Bw;
    UINT32 Total;
    UINT32 MaxLabel = 0;
    UINT32 Tw;

    for (i = 0; i < STORE_BTN_N; i++) {
        Tw = FontStringWidth(gBtnLabel[i]);
        if (Tw > MaxLabel) {
            MaxLabel = Tw;
        }
    }
    Need = MaxLabel + 16u;
    if (Need < 48u) {
        Need = 48u;
    }
    Total = Need * STORE_BTN_N + STORE_BTN_GAP * (STORE_BTN_N - 1u);
    if (Total + STORE_PAD * 2u > Cw && STORE_BTN_N > 0) {
        Bw = (Cw > STORE_PAD * 2u + STORE_BTN_GAP * (STORE_BTN_N - 1u))
                 ? (Cw - STORE_PAD * 2u - STORE_BTN_GAP * (STORE_BTN_N - 1u)) /
                       STORE_BTN_N
                 : 40u;
    } else {
        Bw = Need;
    }
    *OutBw = Bw;
    *OutX0 = Cx + STORE_PAD;
}

static void PaintList(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 Y;
    UINT32 i;
    UINT32 LineH;
    STORE_ENTRY *Tab = StoreScratchTab();
    char Line[96];
    int Li;
    int Inst;
    UINT32 BtnY;
    UINT32 Bw;
    UINT32 Bx0;
    UINT32 Bx;
    UINT32 PadX;
    UINT32 Tw;

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 14) {
        LineH = 14;
    }
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoDrawStringAt(Cx + STORE_PAD, Cy + 4, LocStr(MSG_APP_STORE), COLOR_WHITE);

    Y = Cy + 4 + LineH + 4;
    for (i = 0; i < (UINT32)gCount && Y + STORE_ROW_H < Cy + Ch - STORE_BTN_H - LineH - 8; i++) {
        Inst = StoreIsInstalled(Tab[i].Id);
        Li = 0;
        Line[Li++] = (i == (UINT32)gSel) ? '>' : ' ';
        Line[Li++] = ' ';
        {
            const char *P = Tab[i].Id;
            while (*P && Li < 40) {
                Line[Li++] = *P++;
            }
        }
        Line[Li++] = ' ';
        {
            const char *T = Inst ? "[IN]" : "[--]";
            while (*T && Li < 50) {
                Line[Li++] = *T++;
            }
        }
        Line[Li++] = ' ';
        {
            const char *T = Tab[i].Title;
            while (*T && Li < 90) {
                Line[Li++] = *T++;
            }
        }
        Line[Li] = 0;
        HalVideoDrawStringAt(Cx + STORE_PAD, Y, Line,
                             (i == (UINT32)gSel) ? COLOR_YELLOW : COLOR_WHITE);
        Y += STORE_ROW_H;
    }

    BtnY = Cy + Ch - STORE_BTN_H - LineH - 8;
    StoreBtnGeom(Cx, Cw, &Bw, &Bx0);
    for (i = 0; i < STORE_BTN_N; i++) {
        Bx = Bx0 + i * (Bw + STORE_BTN_GAP);
        Tw = FontStringWidth(gBtnLabel[i]);
        PadX = (Bw > Tw) ? (Bw - Tw) / 2u : 2u;
        UiFillRectangle(Bx, BtnY, Bw, STORE_BTN_H, COLOR_LIGHT_GRAY);
        UiDrawRectangle(Bx, BtnY, Bw, STORE_BTN_H, COLOR_WHITE);
        HalVideoDrawStringAt(Bx + PadX, BtnY + 4, gBtnLabel[i], COLOR_BLACK);
    }

    if (gStatus[0]) {
        HalVideoDrawStringAt(Cx + STORE_PAD, Cy + Ch - LineH - 2, gStatus, COLOR_LIGHT_GRAY);
    }
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
    Reload();
    SetStatus(gCount > 0 ? "click row / Install|Remove|Sync" : "no catalog");
    PaintList();
    DebugWrite("store-ui: open\n");
}

void StoreUiOnClick(UINT32 X, UINT32 Y) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 ListY;
    UINT32 BtnY;
    UINT32 Bw;
    UINT32 Bx0;
    UINT32 Bx;
    UINT32 RelY;
    UINT32 i;
    int Row;
    STORE_ENTRY *Tab = StoreScratchTab();
    int Err;

    if (!StoreUiIsFocused()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 14) {
        LineH = 14;
    }
    ListY = Cy + 4 + LineH + 4;
    BtnY = Cy + Ch - STORE_BTN_H - LineH - 8;
    StoreBtnGeom(Cx, Cw, &Bw, &Bx0);

    if (Y >= BtnY && Y < BtnY + STORE_BTN_H) {
        for (i = 0; i < STORE_BTN_N; i++) {
            Bx = Bx0 + i * (Bw + STORE_BTN_GAP);
            if (X < Bx || X >= Bx + Bw) {
                continue;
            }
            /* Sync 不依赖选中行；Install/Remove 需要 */
            if (i == 2) {
                Err = StoreSyncCatalog();
                SetStatus(Err == 0 ? "sync ok" : "sync fail (need repo)");
                Reload();
                StoreUiRepaint();
                return;
            }
            if (gCount <= 0 || gSel < 0 || gSel >= gCount) {
                SetStatus("no selection");
                StoreUiRepaint();
                return;
            }
            if (i == 0) {
                Err = StoreComboInstall(Tab[gSel].Id);
                SetStatus(Err == 0 ? "installed" : "install fail");
            } else {
                Err = StoreComboRemove(Tab[gSel].Id);
                SetStatus(Err == 0 ? "removed" : "remove fail");
            }
            Reload();
            StoreUiRepaint();
            return;
        }
        return;
    }

    if (Y >= ListY && gCount > 0) {
        RelY = Y - ListY;
        Row = (int)(RelY / STORE_ROW_H);
        if (Row >= 0 && Row < gCount) {
            gSel = Row;
            StoreUiRepaint();
        }
    }
}
