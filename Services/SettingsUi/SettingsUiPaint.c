/*
 * SettingsUiPaint.c — PR-UI-layout-set：令牌三分栏绘制
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Pad;
    UINT32 Gap;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 TextW;
    UINT32 SwW;
    UINT32 SwH;
    char Line[64];
    char Item[40];
    UINT32 Swatch;
    UINT32 PrefW;
    UINT32 PrefH;
    UINT32 NowW;
    UINT32 NowH;
    int HasBody;

    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    Gap = UI_LAYOUT_GAP;
    HalVideoFillRect(X, Y, W, H, ThemePanelDetailBackground());
    if (W > UI_LAYOUT_SEP_W) {
        HalVideoFillRect(X, Y, UI_LAYOUT_SEP_W, H, ThemePanelSeparator());
    }

    ItemLabel(gItemSel, Item, (int)sizeof(Item));
    HasBody = (Item[0] != 0) || gDisplayHint != 0;

    if (!HasBody) {
        /* 空态：垂直居中一行标题 + 一行弱提示 */
        Ty = Y + (H > LineH * 2u + Gap ? (H - LineH * 2u - Gap) / 2u : Pad);
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_DEV_DETAIL), ThemeText());
        Ty += LineH + Gap;
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_CLICK_APPLY), ThemeTextMuted());
        return;
    }

    Ty = Y + Pad;
    MaxY = Y + H - Pad;
    HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_DEV_DETAIL), ThemeText());
    Ty += LineH + Gap;
    HalVideoDrawStringAt(X + Pad, Ty, CatLabel(gCat), ThemeTextMuted());
    Ty += LineH + Gap;

    if (Item[0] && Ty + LineH < MaxY) {
        if ((gCat == SETTINGS_CAT_DESKTOP || gCat == SETTINGS_CAT_SHELL) &&
            Ty + LineH < MaxY && W > Pad * 2u + 48u) {
            Swatch = (gCat == SETTINGS_CAT_DESKTOP)
                         ? ((gItemSel >= 0 && gItemSel < DESKTOP_COLOR_COUNT &&
                             gDesktopColors[gItemSel].Color != DESKTOP_COLOR_WALLPAPER)
                                ? gDesktopColors[gItemSel].Color
                                : ThemeDesktopBackground())
                         : ((gItemSel >= 0 && gItemSel < SHELL_COLOR_COUNT)
                                ? gShellColors[gItemSel].Color
                                : ThemeShellClientBackground());
            TextW = FontStringWidth(Item);
            SwH = LineH > 8u ? LineH - 4u : LineH;
            SwW = 48u;
            HalVideoDrawStringAt(X + Pad, Ty + (LineH > FontCellH() ?
                                                (LineH - FontCellH()) / 2u : 0),
                                 Item, ThemeText());
            if (Pad + TextW + Gap + SwW + Pad <= W) {
                UiFillRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH, Swatch);
                UiDrawRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH,
                                ThemePanelSeparator());
            }
            Ty += LineH + Gap;
        } else {
            HalVideoDrawStringAt(X + Pad, Ty, Item, ThemeText());
            Ty += LineH + Gap;
        }
    }

    if (gCat == SETTINGS_CAT_FONT && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_FONT_SAMPLE), ThemeText());
        Ty += LineH + Gap;
    } else if (gCat == SETTINGS_CAT_DISPLAY) {
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + Pad, Ty,
                                 LocStr(HalCpuIsHypervisor() ? MSG_SET_DISP_QEMU
                                                             : MSG_SET_DISP_PC),
                                 ThemeTextMuted());
            Ty += LineH + 2;
        }
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + Pad, Ty, Line, ThemeTextMuted());
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
                    X + Pad, Ty,
                    LocStr(HalCpuIsHypervisor() ? MSG_SET_PREF_DIFF : MSG_SET_PREF_DIFF_PC),
                    ThemeTextAccent());
                Ty += LineH + 2;
            }
        }
    } else if (gCat == SETTINGS_CAT_SCALE && Ty + LineH * 2 < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_SCALE_HINT1), ThemeTextMuted());
        Ty += LineH;
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_SCALE_HINT2), ThemeTextMuted());
        Ty += LineH + Gap;
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + Pad, Ty, Line, ThemeTextMuted());
            Ty += LineH + Gap;
        }
    } else if (gCat == SETTINGS_CAT_THEME && Ty + LineH * 2 < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_THEME_HINT), ThemeTextMuted());
        Ty += LineH;
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_LIVE_DB), ThemeTextMuted());
        Ty += LineH + Gap;
    } else if (gCat == SETTINGS_CAT_EFFECTS && Ty + LineH * 2 < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_EFFECTS_HINT), ThemeTextMuted());
        Ty += LineH;
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_LIVE_DB), ThemeTextMuted());
        Ty += LineH + Gap;
    }

    if (gDisplayHint == 2 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_APPLIED_LIVE), ThemeTextAccent());
        Ty += LineH;
    } else if (gDisplayHint == 1 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(
            X + Pad, Ty,
            LocStr(HalCpuIsHypervisor() ? MSG_SET_SAVED : MSG_SET_SAVED_PC), ThemeTextAccent());
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + Pad, Ty, LocStr(MSG_SET_CLICK_APPLY), ThemeTextMuted());
    }
}

void PaintMenu(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 ContentX;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 TitleY;
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

    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    UiLayoutTriple(Cw, &SideW, &ListW, &DetailW);
    gSetSideW = SideW;
    gSetPrevW = DetailW;
    TitleY = Cy + Pad;
    ContentX = Cx + SideW;

    if (SideW > 0) {
        gSetSideX = Cx;
        gSetSideY = Cy;
        gSetSideLineH = LineH;
        gSetSideRow0 = TitleY + LineH + UI_LAYOUT_GAP;
        RowW = SideW > Pad * 2u ? SideW - Pad * 2u : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > UI_LAYOUT_SEP_W) {
            HalVideoFillRect(Cx + SideW - UI_LAYOUT_SEP_W, Cy, UI_LAYOUT_SEP_W, Ch,
                             ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + Pad, TitleY, LocStr(MSG_SET_TITLE), ThemeText());
        for (i = 0; i < SETTINGS_CAT_COUNT; i++) {
            UiDrawListRow(Cx + Pad, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          CatLabel((SETTINGS_CAT)i),
                          i == (int)gCat,
                          gSetHoverKind == 0 && gSetHoverIdx == i);
            HitAdd(Cx + Pad, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH, 0, i);
        }
    }

    gSetPrevX = ContentX + ListW;
    gSetListX = ContentX;
    gSetListTop = TitleY + LineH + UI_LAYOUT_GAP;
    gSetListLineH = LineH;
    N = ItemCount();
    gSetListVisible = 1;
    if (Ch > Pad + LineH * 2u) {
        gSetListVisible = (int)((Ch - Pad - LineH * 2u) / LineH);
    }
    if (gSetListVisible < 1) {
        gSetListVisible = 1;
    }
    ClampItemScroll();

    gSetSbVisible = (N > gSetListVisible) ? 1 : 0;
    gSetSbW = UI_LAYOUT_SB_W;
    gSetSbH = (UINT32)gSetListVisible * LineH;
    if (gSetSbH + gSetListTop > Cy + Ch) {
        gSetSbH = (Cy + Ch > gSetListTop) ? (Cy + Ch - gSetListTop) : 0;
    }
    gSetSbX = (ListW > UI_LAYOUT_SB_W + Pad)
                  ? (ContentX + ListW - UI_LAYOUT_SB_W - UI_LAYOUT_GAP)
                  : (ContentX + Pad);
    gSetSbY = gSetListTop;
    gSetListRowW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
    if (gSetSbVisible && gSetListRowW > UI_LAYOUT_SB_W + Pad) {
        gSetListRowW -= (UI_LAYOUT_SB_W + UI_LAYOUT_GAP);
    }

    HalVideoDrawStringAt(ContentX + Pad, TitleY, CatLabel(gCat), ThemeText());

    Applied = CurrentItemIndex();
    RowY = gSetListTop;
    for (i = 0; i < gSetListVisible && gItemScroll + i < N; i++) {
        int Idx = gItemScroll + i;
        int Mark = (Idx == Applied);
        int Sel = (Idx == gItemSel);
        int Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);
        int j;

        ItemLabel(Idx, Label, (int)sizeof(Label));
        Row[0] = Mark ? '*' : ' ';
        Row[1] = ' ';
        for (j = 0; Label[j] && j < (int)sizeof(Row) - 3; j++) {
            Row[j + 2] = Label[j];
        }
        Row[j + 2] = 0;
        UiDrawListRow(ContentX + Pad, RowY, gSetListRowW, LineH, Row, Sel, Hov);
        HitAdd(ContentX + Pad, RowY, gSetListRowW, LineH, 1, Idx);
        RowY += LineH;
    }
    if (gSetSbVisible && gSetSbH > 0) {
        UiDrawScrollBar(gSetSbX, gSetSbY, gSetSbW, gSetSbH, gItemScroll, gSetListVisible, N);
    }

    if (DetailW > 0) {
        DrawDetail(gSetPrevX, Cy, DetailW, Ch);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
