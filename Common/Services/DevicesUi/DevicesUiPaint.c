/*
 * DevicesUiPaint.c — 列表 + 详情（PR-DEV-6）
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

static void DrawDetail(UINT32 Dx, UINT32 Dy, UINT32 Dw, UINT32 Dh) {
    DEVICE_NODE *Dev;
    char Line[80];
    UINTN N;
    int b;
    UINT32 Ty;
    const char *Bound;
    UINT32 V;
    char Tmp[4];
    int T;

    HalVideoFillRect(Dx, Dy, Dw, Dh, ThemePanelDetailBackground());
    UiDrawRectangle(Dx, Dy, Dw, Dh, ThemePanelSeparator());
    Ty = Dy + 6;
    if (gDevUiCount <= 0) {
        HalVideoDrawStringAt(Dx + 8, Ty, LocStr(MSG_DEV_EMPTY), ThemeText());
        return;
    }
    Dev = DeviceGet(gDevUiSel);
    if (!Dev) {
        return;
    }
    HalVideoDrawStringAt(Dx + 8, Ty, LocStr(MSG_DEV_DETAIL), ThemeTextAccent());
    Ty += FontCellH() + 4;
    DevicesUiFormatPci(Dev, Line, sizeof(Line));
    HalVideoDrawStringAt(Dx + 8, Ty, Line, ThemeText());
    Ty += FontCellH() + 2;
    DevicesUiFormatIds(Dev, Line, sizeof(Line));
    HalVideoDrawStringAt(Dx + 8, Ty, Line, ThemeText());
    Ty += FontCellH() + 2;
    HalVideoDrawStringAt(Dx + 8, Ty,
                         Dev->Name[0] ? Dev->Name : "pci", ThemeText());
    Ty += FontCellH() + 2;
    Bound = (Dev->Bound && Dev->Driver && Dev->Driver->Name)
                ? Dev->Driver->Name
                : "-";
    HalVideoDrawStringAt(Dx + 8, Ty, Bound, ThemeText());
    Ty += FontCellH() + 4;
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
    HalVideoDrawStringAt(Dx + 8, Ty, Line, ThemeText());
    Ty += FontCellH() + 2;
    for (b = 0; b < 6 && Ty + FontCellH() < Dy + Dh; b++) {
        if (Dev->Bar[b] == 0) {
            continue;
        }
        N = 0;
        Line[N++] = 'B';
        Line[N++] = (char)('0' + b);
        Line[N++] = '=';
        PutHex8(Line, &N, sizeof(Line), (UINT32)Dev->Bar[b]);
        Line[N] = 0;
        HalVideoDrawStringAt(Dx + 8, Ty, Line, ThemeText());
        Ty += FontCellH() + 2;
    }
}

void DevicesUiPaint(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 ListH;
    UINT32 DetailY;
    int Row;
    int Idx;
    char Line[96];
    UINTN N;
    UINTN i;
    DEVICE_NODE *Dev;
    UINT32 Fg;
    UINT32 RowBg;
    const char *Nm;

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Cw < 40 || Ch < 80) {
        return;
    }
    HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeSettingsClientBackground());

    DetailY = Cy + Ch - DEVUI_DETAIL_H;
    if (DetailY <= Cy + DEVUI_PAD * 2) {
        DetailY = Cy + Ch / 2;
    }
    ListH = DetailY - Cy - DEVUI_PAD;
    gDevUiListX = Cx + DEVUI_PAD;
    gDevUiListY = Cy + DEVUI_PAD;
    gDevUiListW = Cw > DEVUI_PAD * 2 ? Cw - DEVUI_PAD * 2 : Cw;
    gDevUiListH = ListH;
    gDevUiVisible = (int)(ListH / DEVUI_ROW_H);
    if (gDevUiVisible < 1) {
        gDevUiVisible = 1;
    }
    if (gDevUiScroll + gDevUiVisible > gDevUiCount && gDevUiCount > 0) {
        gDevUiScroll = gDevUiCount - gDevUiVisible;
        if (gDevUiScroll < 0) {
            gDevUiScroll = 0;
        }
    }

    HalVideoFillRect(gDevUiListX, gDevUiListY, gDevUiListW, gDevUiListH,
                     ThemePanelSideBackground());
    UiDrawRectangle(gDevUiListX, gDevUiListY, gDevUiListW, gDevUiListH,
                    ThemePanelSeparator());

    if (gDevUiCount <= 0) {
        HalVideoDrawStringAt(gDevUiListX + 8, gDevUiListY + 8,
                             LocStr(MSG_DEV_EMPTY), ThemeText());
    } else {
        for (Row = 0; Row < gDevUiVisible; Row++) {
            Idx = gDevUiScroll + Row;
            if (Idx >= gDevUiCount) {
                break;
            }
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
            DevicesUiFormatIds(Dev, Line + N, sizeof(Line) - N);
            while (Line[N]) {
                N++;
            }
            if (N + 1 < sizeof(Line)) {
                Line[N++] = ' ';
            }
            Nm = Dev->Name[0] ? Dev->Name : "pci";
            for (i = 0; Nm[i] && N + 1 < sizeof(Line); i++) {
                Line[N++] = Nm[i];
            }
            Line[N] = 0;
            HalVideoDrawStringAt(gDevUiListX + 6,
                                 gDevUiListY + (UINT32)Row * DEVUI_ROW_H + 4,
                                 Line, Fg);
        }
    }

    DrawDetail(gDevUiListX, DetailY, gDevUiListW, Cy + Ch - DetailY - 4);
}
