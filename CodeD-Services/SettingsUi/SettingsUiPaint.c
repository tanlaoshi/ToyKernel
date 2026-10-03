/*
 * SettingsUiPaint.c — PR-UI-layout-set：令牌三分栏绘制
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

/* 详情栏软换行；返回 1=画了字 */
static int DrawDetailText(UINT32 X, UINT32 *Ty, UINT32 MaxY, UINT32 MaxW,
                          UINT32 LineStep, const char *S, UINT32 Color) {
    UINT32 Ny;

    if (!S || !Ty || *Ty >= MaxY) {
        return 0;
    }
    Ny = UiDrawTextWrap(X, *Ty, MaxW, MaxY, LineStep, S, Color);
    if (Ny <= *Ty) {
        return 0;
    }
    *Ty = Ny;
    return 1;
}

void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Pad;
    UINT32 Gap;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 TextW;
    UINT32 MaxTextW;
    UINT32 SwW;
    UINT32 SwH;
    UINT32 Step;
    char Line[64];
    char Item[40];
    UINT32 Swatch;
    UINT32 PrefW;
    UINT32 PrefH;
    UINT32 NowW;
    UINT32 NowH;
    int HasBody;

    LineH = UiLayoutRowH();
    Step = FontAdvanceY() + 2u;
    if (Step < 14u) {
        Step = 14u;
    }
    Pad = UI_LAYOUT_PAD;
    Gap = UI_LAYOUT_GAP;
    MaxTextW = W > Pad * 2u ? W - Pad * 2u : W;
    HalVideoFillRect(X, Y, W, H, ThemePanelDetailBackground());
    if (W > UI_LAYOUT_SEP_W) {
        HalVideoFillRect(X, Y, UI_LAYOUT_SEP_W, H, ThemePanelSeparator());
    }

    ItemLabel(gItemSel, Item, (int)sizeof(Item));
    HasBody = (Item[0] != 0) || gDisplayHint != 0;

    if (!HasBody) {
        /* 空态：垂直居中一行标题 + 一行弱提示 */
        Ty = Y + (H > LineH * 2u + Gap ? (H - LineH * 2u - Gap) / 2u : Pad);
        MaxY = Y + H - Pad;
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_DEV_DETAIL),
                             ThemeText());
        Ty += Gap;
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_CLICK_APPLY),
                             ThemeTextMuted());
        return;
    }

    Ty = Y + Pad;
    MaxY = Y + H - Pad;
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_DEV_DETAIL),
                         ThemeText());
    Ty += Gap;
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, CatLabel(gCat),
                         ThemeTextMuted());
    Ty += Gap;

    if (Item[0] && Ty + Step <= MaxY) {
        if ((gCat == SETTINGS_CAT_DESKTOP || gCat == SETTINGS_CAT_SHELL) &&
            Ty + Step <= MaxY && W > Pad * 2u + 48u) {
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
            {
                UINT32 ItemY = Ty + (LineH > FontCellH() ? (LineH - FontCellH()) / 2u : 0);
                (void)DrawDetailText(X + Pad, &ItemY, MaxY, MaxTextW, Step, Item, ThemeText());
            }
            if (Pad + TextW + Gap + SwW + Pad <= W) {
                UiFillRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH, Swatch);
                UiDrawRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH,
                                ThemePanelSeparator());
            }
            Ty += LineH + Gap;
        } else {
            (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Item, ThemeText());
            Ty += Gap;
        }
    }

    if (gCat == SETTINGS_CAT_FONT && Ty + Step <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_FONT_SAMPLE),
                             ThemeText());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_DISPLAY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step,
                             LocStr(HalCpuIsHypervisor() ? MSG_SET_DISP_QEMU
                                                         : MSG_SET_DISP_PC),
                             ThemeTextMuted());
        FormatNowDisplay(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        FormatNowScale(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        if (ThemeHasDisplayPref()) {
            PrefW = ThemeDisplayWidth();
            PrefH = ThemeDisplayHeight();
            HalVideoGetPhysicalSize(&NowW, &NowH);
            if (NowW == 0 || NowH == 0) {
                HalVideoGetSize(&NowW, &NowH);
            }
            if (PrefW != NowW || PrefH != NowH) {
                (void)DrawDetailText(
                    X + Pad, &Ty, MaxY, MaxTextW, Step,
                    LocStr(HalCpuIsHypervisor() ? MSG_SET_PREF_DIFF : MSG_SET_PREF_DIFF_PC),
                    ThemeTextAccent());
            }
        }
    } else if (gCat == SETTINGS_CAT_SCALE && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_SCALE_HINT1),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_SCALE_HINT2),
                             ThemeTextMuted());
        Ty += Gap;
        FormatNowDisplay(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        FormatNowScale(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_THEME && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_THEME_HINT),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_LIVE_DB),
                             ThemeTextMuted());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_EFFECTS && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_EFFECTS_HINT),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_LIVE_DB),
                             ThemeTextMuted());
        Ty += Gap;
    }

    if (gDisplayHint == 2) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_APPLIED_LIVE),
                             ThemeTextAccent());
    } else if (gDisplayHint == 1) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step,
                             LocStr(HalCpuIsHypervisor() ? MSG_SET_SAVED : MSG_SET_SAVED_PC),
                             ThemeTextAccent());
    }
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocStr(MSG_SET_CLICK_APPLY),
                         ThemeTextMuted());
}

static void FormatListRow(int Idx, char *Row, int RowMax) {
    char Label[48];
    int j;
    int Mark;

    if (!Row || RowMax < 4) {
        return;
    }
    ItemLabel(Idx, Label, (int)sizeof(Label));
    Mark = (Idx == CurrentItemIndex());
    Row[0] = Mark ? '*' : ' ';
    Row[1] = ' ';
    for (j = 0; Label[j] && j < RowMax - 3; j++) {
        Row[j + 2] = Label[j];
    }
    Row[j + 2] = 0;
}

static void PaintSideRow(int i, UINT32 Cx, UINT32 Pad, UINT32 RowW) {
    UINT32 Y;

    if (i < 0 || i >= SETTINGS_CAT_COUNT || gSetSideLineH == 0) {
        return;
    }
    Y = gSetSideRow0 + (UINT32)i * gSetSideLineH;
    HalVideoFillRect(Cx + Pad, Y, RowW, gSetSideLineH, ThemePanelSideBackground());
    UiDrawListRow(Cx + Pad, Y, RowW, gSetSideLineH,
                  CatLabel((SETTINGS_CAT)i),
                  i == (int)gCat,
                  gSetHoverKind == 0 && gSetHoverIdx == i);
}

static void PaintListRow(int Idx, UINT32 ListX, UINT32 Pad, UINT32 Bg) {
    UINT32 RowY;
    int Vis;
    char Row[56];
    int Sel;
    int Hov;

    if (Idx < 0 || gSetListLineH == 0) {
        return;
    }
    Vis = Idx - gItemScroll;
    if (Vis < 0 || Vis >= gSetListVisible) {
        return;
    }
    RowY = gSetListTop + (UINT32)Vis * gSetListLineH;
    HalVideoFillRect(ListX + Pad, RowY, gSetListRowW, gSetListLineH, Bg);
    FormatListRow(Idx, Row, (int)sizeof(Row));
    Sel = (Idx == gItemSel);
    Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);
    UiDrawListRow(ListX + Pad, RowY, gSetListRowW, gSetListLineH, Row, Sel, Hov);
}

void PaintHoverDelta(int OldKind, int OldIdx, int NewKind, int NewIdx) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 Pad;
    UINT32 RowW;

    if (GuiFocusKind() != GUI_WIN_SETTINGS) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    Pad = UI_LAYOUT_PAD;
    RowW = gSetSideW > Pad * 2u ? gSetSideW - Pad * 2u : gSetSideW;

    GuiFrameBufferBegin();
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);
    if (OldKind == 0 && OldIdx >= 0) {
        PaintSideRow(OldIdx, Cx, Pad, RowW);
    }
    if (NewKind == 0 && NewIdx >= 0 && (NewKind != OldKind || NewIdx != OldIdx)) {
        PaintSideRow(NewIdx, Cx, Pad, RowW);
    }
    if (OldKind == 1 && OldIdx >= 0) {
        PaintListRow(OldIdx, gSetListX, Pad, Bg);
    }
    if (NewKind == 1 && NewIdx >= 0 && (NewKind != OldKind || NewIdx != OldIdx)) {
        PaintListRow(NewIdx, gSetListX, Pad, Bg);
    }
    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

void PaintMenu(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 ContentX;
    int i;
    int N;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 TitleY;

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

    RowY = gSetListTop;
    for (i = 0; i < gSetListVisible && gItemScroll + i < N; i++) {
        int Idx = gItemScroll + i;
        int Sel = (Idx == gItemSel);
        int Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);
        char Row[56];

        FormatListRow(Idx, Row, (int)sizeof(Row));
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
