/*
 * DevicesUiPaint.c — 三栏：筛选 / 列表 / 详情（PR-DEV-6）
 */
#include "DevicesUiPrivate.h"

static char HexDigit(UINT32 V) {
    V &= 0xFu;
    return (char)(V < 10 ? ('0' + V) : ('a' + V - 10));
}

static void PutHex8(char *Dst, UINTN *N, UINTN Max, UINT32 V) {
    int S;

    for (S = 7; S >= 0 && *N + 1 < Max; S--) {
        Dst[(*N)++] = HexDigit(V >> (UINT32)(S * 4));
    }
}

static const char *FiltLabel(int Filt) {
    if (Filt == DEVUI_FILT_BOUND) {
        return LocaleGet() == LOC_LANG_ZH ? "已绑定" : "Bound";
    }
    if (Filt == DEVUI_FILT_FREE) {
        return LocaleGet() == LOC_LANG_ZH ? "未绑定" : "Free";
    }
    return LocaleGet() == LOC_LANG_ZH ? "全部" : "All";
}

static int DrawLine(UINT32 X, UINT32 *Ty, UINT32 MaxY, const char *S, UINT32 Fg) {
    UINT32 H = FontCellH();

    if (!S || *Ty + H > MaxY) {
        return 0;
    }
    HalVideoDrawStringAt(X, *Ty, S, Fg);
    *Ty += H + 2;
    return 1;
}

static void DrawDetail(UINT32 Dx, UINT32 Dy, UINT32 Dw, UINT32 Dh) {
    DEVICE_NODE *Dev;
    char Line[80];
    UINTN N;
    int b;
    UINT32 Ty;
    UINT32 MaxY;
    const char *Bound;
    UINT32 V;
    char Tmp[4];
    int T;

    HalVideoFillRect(Dx, Dy, Dw, Dh, ThemePanelDetailBackground());
    if (Dw > 3) {
        HalVideoFillRect(Dx, Dy, 3, Dh, ThemePanelSeparator());
    }
    Ty = Dy + 8;
    MaxY = Dy + Dh - 4;
    if (gDevUiFiltCount <= 0) {
        DrawLine(Dx + 10, &Ty, MaxY, LocStr(MSG_DEV_EMPTY), ThemeText());
        return;
    }
    Dev = DeviceGet(gDevUiSel);
    if (!Dev) {
        return;
    }
    DrawLine(Dx + 10, &Ty, MaxY, LocStr(MSG_DEV_DETAIL), ThemeTextAccent());
    DevicesUiFormatPci(Dev, Line, sizeof(Line));
    DrawLine(Dx + 10, &Ty, MaxY, Line, ThemeText());
    DevicesUiFormatIds(Dev, Line, sizeof(Line));
    DrawLine(Dx + 10, &Ty, MaxY, Line, ThemeText());
    DrawLine(Dx + 10, &Ty, MaxY, Dev->Name[0] ? Dev->Name : "pci", ThemeText());
    Bound = (Dev->Bound && Dev->Driver && Dev->Driver->Name) ? Dev->Driver->Name
                                                             : "-";
    DrawLine(Dx + 10, &Ty, MaxY, Bound, ThemeText());
    if (Dev->Compatible[0]) {
        DrawLine(Dx + 10, &Ty, MaxY, Dev->Compatible, ThemeTextMuted());
    }
    N = 0;
    Line[N++] = 'I';
    Line[N++] = 'R';
    Line[N++] = 'Q';
    Line[N++] = '=';
    V = Dev->Irq;
    T = 0;
    if (V == 0) {
        Tmp[T++] = '0';
    } else {
        while (V > 0 && T < 3) {
            Tmp[T++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (T > 0 && N + 1 < sizeof(Line)) {
        Line[N++] = Tmp[--T];
    }
    Line[N] = 0;
    DrawLine(Dx + 10, &Ty, MaxY, Line, ThemeText());
    for (b = 0; b < 6; b++) {
        if (Dev->Bar[b] == 0) {
            continue;
        }
        N = 0;
        Line[N++] = 'B';
        Line[N++] = (char)('0' + b);
        Line[N++] = '=';
        PutHex8(Line, &N, sizeof(Line), (UINT32)Dev->Bar[b]);
        Line[N] = 0;
        if (!DrawLine(Dx + 10, &Ty, MaxY, Line, ThemeText())) {
            break;
        }
    }
}

void DevicesUiPaint(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 SideW;
    UINT32 ContentX, ContentW;
    UINT32 ListW;
    UINT32 LineH;
    UINT32 RowW;
    int Row;
    int Fi;
    int Idx;
    int i;
    char Line[96];
    UINTN N;
    UINTN k;
    DEVICE_NODE *Dev;
    UINT32 Fg;
    UINT32 RowBg;
    const char *Nm;

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Cw < 40 || Ch < 60) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeSettingsClientBackground());
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, ThemeSettingsClientBackground());

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    SideW = 0;
    gDevUiSideW = 0;
    if (Cw > DEVUI_SIDE_W + 280u) {
        SideW = DEVUI_SIDE_W;
    }
    ContentX = Cx + SideW;
    ContentW = Cw - SideW;

    gDevUiPrevW = 0;
    if (ContentW > 360u) {
        gDevUiPrevW = ContentW * 2u / 5u;
        if (gDevUiPrevW < 160u) {
            gDevUiPrevW = 160u;
        }
        if (gDevUiPrevW + 180u > ContentW) {
            gDevUiPrevW = ContentW > 180u ? ContentW - 180u : 0;
        }
    }
    ListW = ContentW - gDevUiPrevW;

    if (SideW > 0) {
        gDevUiSideX = Cx;
        gDevUiSideW = SideW;
        gDevUiSideLineH = LineH;
        gDevUiSideRow0 = Cy + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > 3) {
            HalVideoFillRect(Cx + SideW - 3, Cy, 3, Ch, ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + 8, Cy + 8, LocStr(MSG_APP_DEVICES), ThemeText());
        for (i = 0; i < DEVUI_FILT_N; i++) {
            UiDrawListRow(Cx + 4, gDevUiSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          FiltLabel(i), i == gDevUiFilt, 0);
        }
    }

    gDevUiListX = ContentX + DEVUI_PAD;
    gDevUiListY = Cy + DEVUI_PAD;
    gDevUiListW = ListW > DEVUI_PAD * 2 ? ListW - DEVUI_PAD * 2 : ListW;
    if (gDevUiPrevW > 0) {
        gDevUiListH = Ch > DEVUI_PAD * 2 ? Ch - DEVUI_PAD * 2 : Ch;
    } else {
        gDevUiListH = Ch / 2 > DEVUI_PAD * 2 ? Ch / 2 - DEVUI_PAD * 2 : Ch / 2;
    }
    gDevUiVisible = (int)(gDevUiListH / DEVUI_ROW_H);
    if (gDevUiVisible < 1) {
        gDevUiVisible = 1;
    }
    if (gDevUiScroll + gDevUiVisible > gDevUiFiltCount && gDevUiFiltCount > 0) {
        gDevUiScroll = gDevUiFiltCount - gDevUiVisible;
        if (gDevUiScroll < 0) {
            gDevUiScroll = 0;
        }
    }

    HalVideoFillRect(gDevUiListX, gDevUiListY, gDevUiListW, gDevUiListH,
                     ThemePanelSideBackground());
    UiDrawRectangle(gDevUiListX, gDevUiListY, gDevUiListW, gDevUiListH,
                    ThemePanelSeparator());

    if (gDevUiFiltCount <= 0) {
        HalVideoDrawStringAt(gDevUiListX + 8, gDevUiListY + 8,
                             LocStr(MSG_DEV_EMPTY), ThemeText());
    } else {
        for (Row = 0; Row < gDevUiVisible; Row++) {
            Fi = gDevUiScroll + Row;
            if (Fi >= gDevUiFiltCount) {
                break;
            }
            Idx = gDevUiMap[Fi];
            Dev = DeviceGet(Idx);
            if (!Dev) {
                continue;
            }
            RowBg = (Idx == gDevUiSel) ? ThemeControlAccent()
                                       : ThemePanelSideBackground();
            Fg = (Idx == gDevUiSel) ? ThemeTextOnAccent() : ThemeText();
            HalVideoFillRect(gDevUiListX + 1,
                             gDevUiListY + (UINT32)Row * DEVUI_ROW_H + 1,
                             gDevUiListW > 2 ? gDevUiListW - 2 : 1,
                             DEVUI_ROW_H - 1, RowBg);
            DevicesUiFormatPci(Dev, Line, sizeof(Line));
            N = 0;
            while (Line[N]) {
                N++;
            }
            if (N + 1 < sizeof(Line)) {
                Line[N++] = ' ';
            }
            Nm = Dev->Name[0] ? Dev->Name : "pci";
            for (k = 0; Nm[k] && N + 1 < sizeof(Line); k++) {
                Line[N++] = Nm[k];
            }
            Line[N] = 0;
            HalVideoDrawStringAt(gDevUiListX + 6,
                                 gDevUiListY + (UINT32)Row * DEVUI_ROW_H + 4,
                                 Line, Fg);
        }
    }

    if (gDevUiPrevW > 0) {
        gDevUiPrevX = ContentX + ListW;
        DrawDetail(gDevUiPrevX, Cy, gDevUiPrevW, Ch);
    } else {
        gDevUiPrevX = ContentX + ListW;
        DrawDetail(gDevUiListX, Cy + Ch / 2, gDevUiListW,
                   Ch > Ch / 2 + DEVUI_PAD ? Ch - Ch / 2 - DEVUI_PAD : Ch / 2);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
