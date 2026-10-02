/*
 * StoreUiPaint.c — 左栏 / 列表 / 详情 / 底栏按钮
 * 核心：StoreUi.c
 */
#include "StoreUiPrivate.h"

static void DrawButtons(void) {
    int i;
    UINT32 Bx;
    int ShellBusy = StoreJobShellIsBusy();
    int UiBusy = StoreJobIsBusy() && !ShellBusy;

    for (i = 0; i < STORE_BTN_N; i++) {
        Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
        if (ShellBusy) {
            UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), ThemeTextMuted(),
                           ThemePanelSideBackground(), 0, 0);
        } else if (UiBusy) {
            if (i == 0) {
                UINT32 Face = (gHoverBtn == 0) ? ThemeControlAccent() : ThemeControlFace();
                UINT32 Fg = (gHoverBtn == 0) ? ThemeTextOnAccent() : ThemeText();
                UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, LocStr(MSG_STORE_CANCEL), Fg, Face,
                               gHoverBtn == 0, gPressBtn == 0);
            } else {
                UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), ThemeTextMuted(),
                               ThemePanelSideBackground(), 0, 0);
            }
        } else {
            UINT32 Face = (gHoverBtn == i) ? ThemeControlAccent() : ThemeControlFace();
            UINT32 Fg = (gHoverBtn == i) ? ThemeTextOnAccent() : ThemeText();
            UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), Fg, Face,
                           gHoverBtn == i, gPressBtn == i);
        }
    }
}

static void PutPrefixed(char *Dst, int Max, const char *Pref, const char *Val) {
    int K = 0;

    if (!Dst || Max < 2) {
        return;
    }
    while (Pref && *Pref && K < Max - 1) {
        Dst[K++] = *Pref++;
    }
    while (Val && *Val && K < Max - 1) {
        Dst[K++] = *Val++;
    }
    Dst[K] = 0;
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
    HalVideoDrawStringAt(X + 10, Ty, LocStr(MSG_DEV_DETAIL), ThemeText());
    Ty += LineH + 4;

    E = SelectedEntry();
    MapIdx = (gSel >= 0 && gSel < gFiltCount) ? gMap[gSel] : -1;
    if (!E) {
        HalVideoDrawStringAt(X + 10, Ty, LocStr(MSG_STORE_NO_SEL), ThemeTextMuted());
        return;
    }
    Inst = CachedInstalled(MapIdx);
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, E->Title[0] ? E->Title : E->Id, ThemeText());
        Ty += LineH + 2;
    }
    if (Ty + LineH < MaxY) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_ID), E->Id);
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_TYPE), E->Type);
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
        Ty += LineH;
    }
    if (E->Arch[0] && Ty + LineH < MaxY) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_ARCH), E->Arch);
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
        Ty += LineH;
    }
    if (E->File[0] && Ty + LineH < MaxY) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_FILE), E->File);
        HalVideoDrawStringAt(X + 10, Ty, Line, ThemeTextMuted());
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        char Repo[40];
        StoreUiFormatRepo(Repo, (int)sizeof(Repo));
        HalVideoDrawStringAt(X + 10, Ty, Repo, ThemeTextMuted());
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty,
                             Inst ? LocStr(MSG_STORE_INSTALLED) : LocStr(MSG_STORE_NOT_INST),
                             Inst ? ThemeTextAccent() : ThemeTextMuted());
    }
}

void StorePaintList(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 ContentX;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 FootH;
    UINT32 ListBottom;
    UINT32 TitleY;
    int i;
    STORE_ENTRY *Tab = StoreScratchTab();
    char Row[72];

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    UiLayoutTriple(Cw, &SideW, &ListW, &DetailW);
    gStoreUiSideW = SideW;
    gStoreUiPrevW = DetailW;
    ContentX = Cx + SideW;
    TitleY = Cy + Pad;

    if (SideW > 0) {
        gStoreUiSideX = Cx;
        gStoreUiSideLineH = LineH;
        gStoreUiSideRow0 = TitleY + LineH + UI_LAYOUT_GAP;
        RowW = SideW > Pad * 2u ? SideW - Pad * 2u : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > UI_LAYOUT_SEP_W) {
            HalVideoFillRect(Cx + SideW - UI_LAYOUT_SEP_W, Cy, UI_LAYOUT_SEP_W, Ch,
                             ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + Pad, TitleY, LocStr(MSG_APP_STORE), ThemeText());
        for (i = 0; i < STORE_CAT_COUNT; i++) {
            UiDrawListRow(Cx + Pad, gStoreUiSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          StoreCatLabel(i), i == gStoreUiCat, i == gHoverSide);
        }
    }

    gListX = ContentX;
    FootH = STORE_BTN_H + LineH + Pad + UI_LAYOUT_GAP;
    if (FootH + LineH * 4 > Ch) {
        FootH = STORE_BTN_H + Pad;
    }
    ListBottom = Cy + Ch - FootH;
    gStoreUiListTop = TitleY + LineH + UI_LAYOUT_GAP;
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
    gStoreUiSbX = (ListW > STORE_SB_W + Pad)
                      ? (ContentX + ListW - STORE_SB_W - UI_LAYOUT_GAP)
                      : (ContentX + Pad);
    gStoreUiSbY = gStoreUiListTop;
    gStoreUiListRowW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
    if (gStoreUiSbVisible && gStoreUiListRowW > STORE_SB_W + Pad) {
        gStoreUiListRowW -= (STORE_SB_W + UI_LAYOUT_GAP);
    }

    HalVideoDrawStringAt(ContentX + Pad, TitleY, StoreCatLabel(gStoreUiCat), ThemeText());

    RowY = gStoreUiListTop;
    for (i = 0; i < gStoreUiListVisible && gStoreUiScroll + i < gFiltCount; i++) {
        int Fi = gStoreUiScroll + i;
        int Ci = gMap[Fi];
        int Inst = CachedInstalled(Ci);
        int k = 0;
        const char *P;

        Row[k++] = Inst ? '*' : ' ';
        Row[k++] = ' ';
        Row[k++] = '[';
        if (Tab[Ci].Origin == STORE_SRC_NET) {
            Row[k++] = 'N';
            Row[k++] = 'e';
            Row[k++] = 't';
        } else {
            Row[k++] = 'L';
            Row[k++] = 'o';
            Row[k++] = 'c';
        }
        Row[k++] = ']';
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
        UiDrawListRow(ContentX + Pad, RowY, gStoreUiListRowW, LineH, Row,
                      Fi == gSel, Fi == gHoverRow);
        RowY += LineH;
    }
    if (gStoreUiSbVisible && gStoreUiSbH > 0) {
        UiDrawScrollBar(gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH, gStoreUiScroll,
                        gStoreUiListVisible, gFiltCount);
    }
    if (gFiltCount == 0) {
        HalVideoDrawStringAt(ContentX + Pad, gStoreUiListTop + 4, LocStr(MSG_STORE_EMPTY),
                             ThemeTextMuted());
    }

    gBtnY = ListBottom + UI_LAYOUT_GAP;
    StoreBtnGeom(ContentX, ListW);
    DrawButtons();
    if (gStoreUiStatus[0]) {
        HalVideoDrawStringAt(ContentX + Pad, Cy + Ch - LineH - 2, gStoreUiStatus,
                             ThemeTextMuted());
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
