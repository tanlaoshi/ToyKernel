/*
 * SettingsUiPaint.c — 三分栏绘制
 * 核心：SettingsUi.c
 */
#include "SettingsUiPriv.h"

void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Ty;
    UINT32 MaxY;
    char Line[64];
    char Item[40];
    UINT32 Swatch;
    UINT32 PrefW;
    UINT32 PrefH;
    UINT32 NowW;
    UINT32 NowH;

    LineH = FontAdvanceY();
    if (LineH < 14) {
        LineH = 14;
    }
    HalVideoFillRect(X, Y, W, H, SETTINGS_PREV_BG);
    if (W > 3) {
        HalVideoFillRect(X, Y, 3, H, COLOR_DARK_GRAY);
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    HalVideoDrawStringAt(X + 10, Ty, "Detail", COLOR_BLACK);
    Ty += LineH + 4;
    HalVideoDrawStringAt(X + 10, Ty, CatLabel(gCat), COLOR_DARK_GRAY);
    Ty += LineH + 2;

    ItemLabel(gItemSel, Item, (int)sizeof(Item));
    if (Item[0] && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, Item, COLOR_BLACK);
        Ty += LineH + 4;
    }

    if (gCat == SETTINGS_CAT_DESKTOP || gCat == SETTINGS_CAT_SHELL) {
        Swatch = (gCat == SETTINGS_CAT_DESKTOP)
                     ? ((gItemSel >= 0 && gItemSel < DESKTOP_COLOR_COUNT)
                            ? gDesktopColors[gItemSel].Color
                            : ThemeDesktopBackground())
                     : ((gItemSel >= 0 && gItemSel < SHELL_COLOR_COUNT)
                            ? gShellColors[gItemSel].Color
                            : ThemeShellClientBackground());
        if (Ty + 36 < MaxY && W > 24) {
            UiFillRectangle(X + 10, Ty, W > 40 ? 48 : W - 20, 28, Swatch);
            UiDrawRectangle(X + 10, Ty, W > 40 ? 48 : W - 20, 28, COLOR_DARK_GRAY);
            Ty += 36;
        }
    } else if (gCat == SETTINGS_CAT_FONT && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "The quick brown fox", COLOR_BLACK);
        Ty += LineH + 4;
    } else if (gCat == SETTINGS_CAT_DISPLAY) {
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty,
                                 HalCpuIsHypervisor()
                                     ? "Change may need quit QEMU + rerun"
                                     : "Change may need reboot to apply",
                                 COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
        if (ThemeHasDisplayPref()) {
            PrefW = ThemeDisplayWidth();
            PrefH = ThemeDisplayHeight();
            HalVideoGetPhysicalSize(&NowW, &NowH);
            if (NowW == 0 || NowH == 0) {
                HalVideoGetSize(&NowW, &NowH);
            }
            if ((PrefW != NowW || PrefH != NowH) && Ty + LineH < MaxY) {
                HalVideoDrawStringAt(
                    X + 10, Ty,
                    LocStr(HalCpuIsHypervisor() ? MSG_SET_PREF_DIFF : MSG_SET_PREF_DIFF_PC),
                    COLOR_BLUE);
                Ty += LineH + 2;
            }
        }
    } else if (gCat == SETTINGS_CAT_SCALE && Ty + LineH * 2 < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "50=small 100=normal", COLOR_DARK_GRAY);
        Ty += LineH;
        HalVideoDrawStringAt(X + 10, Ty, "150/200=large", COLOR_DARK_GRAY);
        Ty += LineH + 2;
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
    }

    if (gDisplayHint == 2 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "Applied (live)", COLOR_BLUE);
        Ty += LineH;
    } else if (gDisplayHint == 1 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(
            X + 10, Ty,
            LocStr(HalCpuIsHypervisor() ? MSG_SET_SAVED : MSG_SET_SAVED_PC), COLOR_BLUE);
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "Click item to apply", COLOR_DARK_GRAY);
    }
}

void PaintMenu(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 SideW;
    UINT32 ContentX, ContentW;
    UINT32 ListW;
    UINT32 RowY;
    UINT32 RowW;
    int i;
    int N;
    int Applied;
    char Label[48];
    char Row[56];

    if (GuiFocusKind() != GUI_WIN_SETTINGS) {
        if (!FocusSettingsWindow()) {
            return;
        }
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }

    HitClear();
    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    SideW = 0;
    gSetSideW = 0;
    if (Cw > SETTINGS_SIDE_W + 160u) {
        SideW = SETTINGS_SIDE_W;
    }
    ContentX = Cx + SideW;
    ContentW = Cw - SideW;

    if (SideW > 0) {
        gSetSideX = Cx;
        gSetSideY = Cy;
        gSetSideW = SideW;
        gSetSideLineH = LineH;
        gSetSideRow0 = Cy + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, SETTINGS_SIDE_BG);
        if (SideW > 3) {
            HalVideoFillRect(Cx + SideW - 3, Cy, 3, Ch, COLOR_DARK_GRAY);
        }
        HalVideoDrawStringAt(Cx + 8, Cy + 8, LocStr(MSG_SET_TITLE), COLOR_BLACK);
        for (i = 0; i < SETTINGS_CAT_COUNT; i++) {
            UiDrawListRow(Cx + 4, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          CatLabel((SETTINGS_CAT)i),
                          i == (int)gCat,
                          gSetHoverKind == 0 && gSetHoverIdx == i);
            HitAdd(Cx + 4, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH, 0, i);
        }
    }

    gSetPrevW = 0;
    gSetPrevX = ContentX;
    if (ContentW > 360u) {
        gSetPrevW = ContentW * 2u / 5u;
        if (gSetPrevW < 160u) {
            gSetPrevW = 160u;
        }
        if (gSetPrevW + 120u > ContentW) {
            gSetPrevW = ContentW > 120u ? ContentW - 120u : 0;
        }
    }

    ListW = ContentW - gSetPrevW;
    gSetListX = ContentX;
    gSetListTop = Cy + 8 + LineH + 4;
    gSetListLineH = LineH;
    N = ItemCount();
    gSetListVisible = 1;
    if (Ch > 8 + LineH * 2) {
        gSetListVisible = (int)((Ch - 8 - LineH * 2) / LineH);
    }
    if (gSetListVisible < 1) {
        gSetListVisible = 1;
    }
    ClampItemScroll();

    gSetSbVisible = (N > gSetListVisible) ? 1 : 0;
    gSetSbW = SETTINGS_SB_W;
    gSetSbH = (UINT32)gSetListVisible * LineH;
    if (gSetSbH + gSetListTop > Cy + Ch) {
        gSetSbH = (Cy + Ch > gSetListTop) ? (Cy + Ch - gSetListTop) : 0;
    }
    gSetSbX = (ListW > SETTINGS_SB_W + 8) ? (ContentX + ListW - SETTINGS_SB_W - 4)
                                      : (ContentX + 4);
    if (gSetPrevW > 0 && gSetSbX + gSetSbW > ContentX + ListW) {
        gSetSbX = ContentX + 4;
    }
    gSetSbY = gSetListTop;
    gSetListRowW = ListW > 8 ? ListW - 8 : ListW;
    if (gSetSbVisible && gSetListRowW > SETTINGS_SB_W + 8) {
        gSetListRowW -= (SETTINGS_SB_W + 4);
    }

    HalVideoDrawStringAt(ContentX + 8, Cy + 8, CatLabel(gCat), COLOR_BLACK);

    Applied = CurrentItemIndex();
    RowY = gSetListTop;
    for (i = 0; i < gSetListVisible && gItemScroll + i < N; i++) {
        int Idx = gItemScroll + i;
        int Mark = (Idx == Applied);
        int Sel = (Idx == gItemSel);
        int Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);

        ItemLabel(Idx, Label, (int)sizeof(Label));
        Row[0] = Mark ? '*' : ' ';
        Row[1] = ' ';
        {
            int j;
            for (j = 0; Label[j] && j < (int)sizeof(Row) - 3; j++) {
                Row[j + 2] = Label[j];
            }
            Row[j + 2] = 0;
        }
        UiDrawListRow(ContentX + 4, RowY, gSetListRowW, LineH, Row, Sel, Hov);
        HitAdd(ContentX + 4, RowY, gSetListRowW, LineH, 1, Idx);
        RowY += LineH;
    }
    if (gSetSbVisible && gSetSbH > 0) {
        UiDrawScrollBar(gSetSbX, gSetSbY, gSetSbW, gSetSbH, gItemScroll, gSetListVisible, N);
    }

    if (gSetPrevW > 0) {
        gSetPrevX = ContentX + ListW;
        DrawDetail(gSetPrevX, Cy, gSetPrevW, Ch);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
