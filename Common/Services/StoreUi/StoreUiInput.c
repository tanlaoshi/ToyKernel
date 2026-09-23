/*
 * StoreUiInput.c — 点击、悬停、装卸队列
 * 核心：StoreUi.c
 */
#include "StoreUiPrivate.h"

static void DoBtn(int Btn) {
    STORE_ENTRY *E;
    char Buf[80];
    int i;
    int j;
    const char *Verb;

    if (StoreJobShellIsBusy()) {
        StoreSetStatus("shell busy");
        StoreUiRepaint();
        return;
    }
    if (StoreJobIsBusy()) {
        if (Btn == 0) {
            if (StoreJobCancel() == 0) {
                StoreSetStatus("cancelling...");
            } else {
                StoreSetStatus("busy...");
            }
        } else {
            StoreSetStatus("busy...");
        }
        StoreUiRepaint();
        return;
    }
    gHoverBtn = -1;
    gPressBtn = -1;
    if (Btn == 2) {
        if (StoreJobEnqueue(STORE_JOB_SYNC, 0) != 0) {
            StoreSetStatus("busy...");
        } else {
            StoreSetStatus("sync: catalog");
        }
        StoreUiRepaint();
        return;
    }
    E = SelectedEntry();
    if (!E) {
        StoreSetStatus("no selection");
        StoreUiRepaint();
        return;
    }
    if (Btn == 0) {
        if (StoreIsInstalled(E->Id)) {
            StoreSetStatus("already installed");
            StoreUiRepaint();
            return;
        }
        Verb = "install";
        if (StoreJobEnqueue(STORE_JOB_INSTALL, E->Id) != 0) {
            StoreSetStatus("busy...");
            StoreUiRepaint();
            return;
        }
    } else {
        if (!StoreIsInstalled(E->Id)) {
            StoreSetStatus("not installed");
            StoreUiRepaint();
            return;
        }
        Verb = "remove";
        if (StoreJobEnqueue(STORE_JOB_REMOVE, E->Id) != 0) {
            StoreSetStatus("busy...");
            StoreUiRepaint();
            return;
        }
    }
    i = 0;
    while (Verb[i] && i < 24) {
        Buf[i] = Verb[i];
        i++;
    }
    Buf[i++] = ':';
    Buf[i++] = ' ';
    for (j = 0; E->Id[j] && i < 78; j++) {
        Buf[i++] = E->Id[j];
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
    StoreUiRepaint();
}

void StoreUiPump(void) {
    /*
     * Job 由 WorkerTask 推进（后台）。此处刻意不 Step：
     * 旧路径 GuiPollMouse→Pump→StoreRemove 会占死 Gui，装卸期鼠标必卡。
     */
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

    if (gStoreUiSideW > 0 && X < Cx + gStoreUiSideW) {
        if (Y >= gStoreUiSideRow0) {
            Row = (int)((Y - gStoreUiSideRow0) / gStoreUiSideLineH);
            if (Row >= 0 && Row < STORE_CAT_COUNT) {
                gStoreUiCat = Row;
                gStoreUiScroll = 0;
                RebuildFilter();
                StoreSetStatus(StoreCatLabel(gStoreUiCat));
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

    if (gStoreUiSbVisible &&
        UiScrollBarHit(gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH, gStoreUiScroll, gStoreUiListVisible, gFiltCount,
                       X, Y, &First)) {
        gStoreUiScroll = First;
        StoreUiRepaint();
        return;
    }

    if (gStoreUiPrevW > 0 && X >= gStoreUiPrevX) {
        return;
    }
    if (Y >= gStoreUiListTop && Y < gBtnY && gFiltCount > 0) {
        Row = UiListRowFromY(gStoreUiListTop, gStoreUiListLineH, gStoreUiListVisible, Y);
        if (Row >= 0) {
            int Fi = gStoreUiScroll + Row;
            if (Fi >= 0 && Fi < gFiltCount) {
                gSel = Fi;
                StoreUiRepaint();
            }
        }
    }
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
    int Running = StoreJobIsRunning();

    /* 装卸中：只允许 Cancel 钮，禁列表悬停重绘 */
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

    if (!Running && gStoreUiSideW > 0 && X >= Cx && X < Cx + gStoreUiSideW &&
        Y >= gStoreUiSideRow0) {
        Side = (int)((Y - gStoreUiSideRow0) / gStoreUiSideLineH);
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
        if (Running && Btn > 0) {
            Btn = -1;
        }
    } else if (!Running && !(gStoreUiPrevW > 0 && X >= gStoreUiPrevX) &&
               Y >= gStoreUiListTop && Y < gBtnY && gFiltCount > 0) {
        Row = UiListRowFromY(gStoreUiListTop, gStoreUiListLineH, gStoreUiListVisible, Y);
        if (Row >= 0) {
            int Fi = gStoreUiScroll + Row;
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
        } else if (!Running && Side >= 0) {
            FireSide = Side;
        } else if (!Running && Row >= 0) {
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
        gStoreUiCat = FireSide;
        gStoreUiScroll = 0;
        RebuildFilter();
        StoreSetStatus(StoreCatLabel(gStoreUiCat));
        StoreUiRepaint();
    } else if (FireRow >= 0) {
        gSel = FireRow;
        StoreUiRepaint();
    } else if (FireBtn >= 0) {
        DoBtn(FireBtn);
    }
}
