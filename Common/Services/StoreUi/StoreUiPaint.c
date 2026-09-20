/*
 * StoreUiPaint.c — 左栏 / 列表 / 详情 / 底栏按钮
 * 核心：StoreUi.c
 */
#include "StoreUiPrivate.h"

static void DrawButtons(void) {
    int i;
    UINT32 Bx;

    for (i = 0; i < STORE_BTN_N; i++) {
        Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
        {
            UINT32 Face = (gHoverBtn == i) ? ThemeControlAccent() : ThemeControlFace();
            UINT32 Fg = (gHoverBtn == i) ? ThemeTextOnAccent() : ThemeText();
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
    HalVideoFillRect(X, Y, W, H, ThemePanelDetailBackground());
    if (W > 3) {
        HalVideoFillRect(X, Y, 3, H, ThemePanelSeparator());
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    HalVideoDrawStringAt(X + 10, Ty, "Detail", ThemeText());
    Ty += LineH + 4;

    E = SelectedEntry();
    MapIdx = (gSel >= 0 && gSel < gFiltCount) ? gMap[gSel] : -1;
    if (!E) {
        HalVideoDrawStringAt(X + 10, Ty, "(no selection)", ThemeTextMuted());
        return;
    }
    Inst = CachedInstalled(MapIdx);
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, E->Title[0] ? E->Title : E->Id, ThemeText());
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
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
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
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
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
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
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
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
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
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, Inst ? "status: installed" : "status: not installed",
                             Inst ? ThemeTextAccent() : ThemeTextMuted());
    }
}

void StorePaintList(void) {
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
    gStoreUiSideW = 0;
    if (Cw > STORE_SIDE_W + 160u) {
        SideW = STORE_SIDE_W;
    }
    ContentX = Cx + SideW;
    ContentW = Cw - SideW;

    if (SideW > 0) {
        gStoreUiSideX = Cx;
        gStoreUiSideW = SideW;
        gStoreUiSideLineH = LineH;
        gStoreUiSideRow0 = Cy + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > 3) {
            HalVideoFillRect(Cx + SideW - 3, Cy, 3, Ch, ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + 8, Cy + 8, LocStr(MSG_APP_STORE), ThemeText());
        for (i = 0; i < STORE_CAT_COUNT; i++) {
            UiDrawListRow(Cx + 4, gStoreUiSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          StoreCatLabel(i), i == gStoreUiCat, i == gHoverSide);
        }
    }

    gStoreUiPrevW = 0;
    if (ContentW > 380u) {
        gStoreUiPrevW = ContentW * 2u / 5u;
        if (gStoreUiPrevW < 180u) {
            gStoreUiPrevW = 180u;
        }
        if (gStoreUiPrevW + 200u > ContentW) {
            gStoreUiPrevW = ContentW > 200u ? ContentW - 200u : 0;
        }
    }

    ListW = ContentW - gStoreUiPrevW;
    gListX = ContentX;
    FootH = STORE_BTN_H + LineH + 16u;
    if (FootH + LineH * 4 > Ch) {
        FootH = STORE_BTN_H + 12u;
    }
    ListBottom = Cy + Ch - FootH;
    gStoreUiListTop = Cy + 8 + LineH + 4;
    gStoreUiListLineH = LineH;
    gStoreUiListVisible = 1;
    if (ListBottom > gStoreUiListTop + LineH) {
        gStoreUiListVisible = (int)((ListBottom - gStoreUiListTop) / LineH);
    }
    if (gStoreUiListVisible < 1) {
        gStoreUiListVisible = 1;
    }
    ClampScroll();

    gStoreUiSbVisible = (gFiltCount > gStoreUiListVisible) ? 1 : 0;
    gStoreUiSbW = STORE_SB_W;
    gStoreUiSbH = (UINT32)gStoreUiListVisible * LineH;
    if (gStoreUiSbH + gStoreUiListTop > ListBottom) {
        gStoreUiSbH = (ListBottom > gStoreUiListTop) ? (ListBottom - gStoreUiListTop) : 0;
    }
    gStoreUiSbX = (ListW > STORE_SB_W + 8) ? (ContentX + ListW - STORE_SB_W - 4)
                                    : (ContentX + 4);
    gStoreUiSbY = gStoreUiListTop;
    gStoreUiListRowW = ListW > 8 ? ListW - 8 : ListW;
    if (gStoreUiSbVisible && gStoreUiListRowW > STORE_SB_W + 8) {
        gStoreUiListRowW -= (STORE_SB_W + 4);
    }

    HalVideoDrawStringAt(ContentX + 8, Cy + 8, StoreCatLabel(gStoreUiCat), ThemeText());

    RowY = gStoreUiListTop;
    for (i = 0; i < gStoreUiListVisible && gStoreUiScroll + i < gFiltCount; i++) {
        int Fi = gStoreUiScroll + i;
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
        UiDrawListRow(ContentX + 4, RowY, gStoreUiListRowW, LineH, Row,
                      Fi == gSel, Fi == gHoverRow);
        RowY += LineH;
    }
    if (gStoreUiSbVisible && gStoreUiSbH > 0) {
        UiDrawScrollBar(gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH, gStoreUiScroll, gStoreUiListVisible, gFiltCount);
    }
    if (gFiltCount == 0) {
        HalVideoDrawStringAt(ContentX + 12, gStoreUiListTop + 4, "(empty)", ThemeTextMuted());
    }

    /* 第 2 分栏下方：三钮均分中栏宽度 */
    gBtnY = ListBottom + 4;
    StoreBtnGeom(ContentX, ListW);
    DrawButtons();
    if (gStoreUiStatus[0]) {
        HalVideoDrawStringAt(ContentX + 8, Cy + Ch - LineH - 2, gStoreUiStatus, ThemeTextMuted());
    }

    if (gStoreUiPrevW > 0) {
        gStoreUiPrevX = ContentX + ListW;
        DrawDetail(gStoreUiPrevX, Cy, gStoreUiPrevW, Ch);
    } else {
        gStoreUiPrevX = ContentX + ListW;
        gStoreUiPrevW = 0;
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
