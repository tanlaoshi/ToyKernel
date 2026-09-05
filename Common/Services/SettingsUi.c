/*
 * SettingsUi.c — Settings 菜单（PR-D5/D7 + PR-G12 控件化）
 *
 * 分辨率经 ThemeSave → TOYOS.DB + THEME.CFG mode=WxH。
 * QEMU：退出后 ./run-split.sh（edid from rootfs）；真机：Guest reboot。
 * PR-G12：主/子页用 UiDrawButton；鼠标点选与数字键等价。
 */
#include "SettingsUi.h"
#include "Gui.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Debug.h"
#include "Locale.h"

typedef enum {
    SETTINGS_PAGE_MAIN = 0,
    SETTINGS_PAGE_DESKTOP_BG,
    SETTINGS_PAGE_SHELL_BG,
    SETTINGS_PAGE_FONT,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_LANGUAGE
} SETTINGS_PAGE;

typedef struct {
    const char *Label;
    UINT32      Color;
} SETTINGS_COLOR;

typedef struct {
    const char *Label;
    UINT32      W;
    UINT32      H;
} SETTINGS_MODE;

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    int    Action; /* 与数字键相同：0=返回，1..=选项 */
} SETTINGS_HIT;

#define SETTINGS_HIT_MAX 16

static SETTINGS_PAGE gPage = SETTINGS_PAGE_MAIN;
static int gRebootHint;
static SETTINGS_HIT gHits[SETTINGS_HIT_MAX];
static int gHitCount;

static const SETTINGS_COLOR gDesktopColors[] = {
    { "Dark Gray", COLOR_DARK_GRAY },
    { "Blue",      COLOR_BLUE },
    { "Green",     COLOR_GREEN },
    { "Black",     COLOR_BLACK },
    { "Gray",      COLOR_GRAY },
};

static const SETTINGS_COLOR gShellColors[] = {
    { "Light Gray", COLOR_LIGHT_GRAY },
    { "White",      COLOR_WHITE },
    { "Cyan",       COLOR_CYAN },
    { "Yellow",     COLOR_YELLOW },
    { "Gray",       COLOR_GRAY },
};

/* 与路线图 / ToyBoot QEMU 友好表对齐 */
static const SETTINGS_MODE gModes[] = {
    { "800x600",   800,  600 },
    { "1024x768", 1024,  768 },
    { "1280x720", 1280,  720 },
    { "1600x900", 1600,  900 },
};

#define DESKTOP_COLOR_COUNT \
    ((int)(sizeof(gDesktopColors) / sizeof(gDesktopColors[0])))
#define SHELL_COLOR_COUNT \
    ((int)(sizeof(gShellColors) / sizeof(gShellColors[0])))
#define MODE_COUNT ((int)(sizeof(gModes) / sizeof(gModes[0])))

static int FocusSettingsWindow(void) {
    int i;

    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        return 1;
    }
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) == GUI_WIN_SETTINGS) {
            GuiRaiseToFront(i);
            return 1;
        }
    }
    return 0;
}

static void FormatUxU(char *Out, UINTN Max, UINT32 A, UINT32 B) {
    UINTN N = 0;
    char Tmp[8];
    int Tn;
    int i;
    UINT32 V;

    if (Max == 0) {
        return;
    }
    V = A;
    Tn = 0;
    if (V == 0) {
        Tmp[Tn++] = '0';
    } else {
        while (V > 0 && Tn < (int)sizeof(Tmp)) {
            Tmp[Tn++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    for (i = Tn - 1; i >= 0 && N + 1 < Max; i--) {
        Out[N++] = Tmp[i];
    }
    if (N + 1 < Max) {
        Out[N++] = 'x';
    }
    V = B;
    Tn = 0;
    if (V == 0) {
        Tmp[Tn++] = '0';
    } else {
        while (V > 0 && Tn < (int)sizeof(Tmp)) {
            Tmp[Tn++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    for (i = Tn - 1; i >= 0 && N + 1 < Max; i--) {
        Out[N++] = Tmp[i];
    }
    Out[N] = 0;
}

static void CopyLabel(char *Dst, int DstMax, const char *S) {
    int j;

    if (DstMax <= 0) {
        return;
    }
    for (j = 0; S[j] && j < DstMax - 1; j++) {
        Dst[j] = S[j];
    }
    Dst[j] = 0;
}

static void HitClear(void) {
    gHitCount = 0;
}

static void HitAdd(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Action) {
    if (gHitCount >= SETTINGS_HIT_MAX || W == 0 || H == 0) {
        return;
    }
    gHits[gHitCount].X = X;
    gHits[gHitCount].Y = Y;
    gHits[gHitCount].W = W;
    gHits[gHitCount].H = H;
    gHits[gHitCount].Action = Action;
    gHitCount++;
}

static void DrawHint(UINT32 X, UINT32 *Y, UINT32 MaxBottom, const char *Text, UINT32 Color) {
    if (*Y + FontCellH() > MaxBottom) {
        return;
    }
    HalVideoDrawStringAt(X, *Y, Text, Color);
    *Y += FontAdvanceY();
}

static void DrawButtonRow(UINT32 X, UINT32 *Y, UINT32 Bw, UINT32 Bh, UINT32 Gap,
                          UINT32 MaxBottom, const char *Text, int Selected,
                          int Action) {
    UINT32 Bg;
    UINT32 Fg;

    if (*Y + Bh > MaxBottom) {
        return;
    }
    Bg = Selected ? COLOR_BLUE : COLOR_LIGHT_GRAY;
    Fg = Selected ? COLOR_WHITE : COLOR_BLACK;
    UiDrawButton(X, *Y, Bw, Bh, Text, Fg, Bg);
    HitAdd(X, *Y, Bw, Bh, Action);
    *Y += Bh + Gap;
}

static void PaintMenu(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 X0;
    UINT32 Y;
    UINT32 MaxBottom;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Gap;
    int i;
    char Line[56];
    char Btn[56];
    const FONT_FACE *Face;
    UINT32 CurColor;
    UINT32 CurFont;
    UINT32 NowW;
    UINT32 NowH;
    UINT32 PrefW;
    UINT32 PrefH;
    int HasPref;

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

    if (Cw > 8 && Ch > 8) {
        UiDrawRectangle(Cx + 4, Cy + 4, Cw - 8, Ch - 8, COLOR_DARK_GRAY);
    }

    X0 = Cx + 12;
    Y = Cy + 8;
    MaxBottom = Cy + Ch - 4;
    Bw = Cw > 24 ? Cw - 24 : Cw;
    Bh = FontCellH() + 10;
    if (Bh < 24) {
        Bh = 24;
    }
    Gap = 4;

    DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_TITLE), COLOR_BLUE);
    DrawHint(X0, &Y, MaxBottom, "----------------", COLOR_DARK_GRAY);
    Y += 4;

    if (gPage == SETTINGS_PAGE_MAIN) {
        HalVideoGetSize(&NowW, &NowH);
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_MAIN), COLOR_BLACK);
        Y += 2;
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_DESKTOP_BG), 0, 1);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_SHELL_BG), 0, 2);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_FONT), 0, 3);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_DISPLAY), 0, 4);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_LANGUAGE), 0, 5);
        Line[0] = 'N';
        Line[1] = 'o';
        Line[2] = 'w';
        Line[3] = ' ';
        FormatUxU(Line + 4, sizeof(Line) - 4, NowW, NowH);
        if (ThemeHasDisplayPref()) {
            UINTN N = 0;
            while (Line[N]) {
                N++;
            }
            if (N + 6 < sizeof(Line)) {
                Line[N++] = ' ';
                Line[N++] = 'n';
                Line[N++] = 'x';
                Line[N++] = 't';
                Line[N++] = ' ';
                FormatUxU(Line + N, sizeof(Line) - N,
                          ThemeDisplayWidth(), ThemeDisplayHeight());
            }
        } else {
            UINTN N = 0;
            while (Line[N]) {
                N++;
            }
            CopyLabel(Line + N, (int)(sizeof(Line) - N), " nxt auto");
        }
        DrawHint(X0, &Y, MaxBottom, Line, COLOR_DARK_GRAY);
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_HINT_MAIN), COLOR_DARK_GRAY);
        if (gRebootHint) {
            DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_SAVED), COLOR_BLUE);
        }
    } else if (gPage == SETTINGS_PAGE_DESKTOP_BG) {
        CurColor = ThemeDesktopBg();
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_PAGE_DESKTOP), COLOR_BLACK);
        Y += 2;
        for (i = 0; i < DESKTOP_COLOR_COUNT; i++) {
            Btn[0] = (gDesktopColors[i].Color == CurColor) ? '*' : ' ';
            Btn[1] = ' ';
            CopyLabel(Btn + 2, (int)sizeof(Btn) - 2, gDesktopColors[i].Label);
            DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, Btn,
                          gDesktopColors[i].Color == CurColor, i + 1);
        }
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_HINT_BACK), 0, 0);
    } else if (gPage == SETTINGS_PAGE_SHELL_BG) {
        CurColor = ThemeShellClientBg();
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_PAGE_SHELL), COLOR_BLACK);
        Y += 2;
        for (i = 0; i < SHELL_COLOR_COUNT; i++) {
            Btn[0] = (gShellColors[i].Color == CurColor) ? '*' : ' ';
            Btn[1] = ' ';
            CopyLabel(Btn + 2, (int)sizeof(Btn) - 2, gShellColors[i].Label);
            DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, Btn,
                          gShellColors[i].Color == CurColor, i + 1);
        }
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_HINT_BACK), 0, 0);
    } else if (gPage == SETTINGS_PAGE_FONT) {
        CurFont = ThemeFontId();
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_PAGE_FONT), COLOR_BLACK);
        Y += 2;
        for (i = 0; (UINT32)i < FontCount(); i++) {
            Face = FontGetById((UINT32)i);
            Btn[0] = ((UINT32)i == CurFont) ? '*' : ' ';
            Btn[1] = ' ';
            CopyLabel(Btn + 2, (int)sizeof(Btn) - 2,
                      (Face && Face->Name) ? Face->Name : "?");
            DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, Btn,
                          (UINT32)i == CurFont, i + 1);
        }
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_HINT_BACK), 0, 0);
    } else if (gPage == SETTINGS_PAGE_DISPLAY) {
        HasPref = ThemeHasDisplayPref();
        PrefW = ThemeDisplayWidth();
        PrefH = ThemeDisplayHeight();
        HalVideoGetSize(&NowW, &NowH);
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_PAGE_DISPLAY), COLOR_BLACK);
        Y += 2;
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, "Auto", !HasPref, 1);
        for (i = 0; i < MODE_COUNT; i++) {
            int Mark = HasPref && gModes[i].W == PrefW && gModes[i].H == PrefH;
            DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, gModes[i].Label, Mark, 2 + i);
        }
        Line[0] = 'N';
        Line[1] = 'o';
        Line[2] = 'w';
        Line[3] = ' ';
        FormatUxU(Line + 4, sizeof(Line) - 4, NowW, NowH);
        DrawHint(X0, &Y, MaxBottom, Line, COLOR_DARK_GRAY);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_HINT_BACK), 0, 0);
        if (gRebootHint) {
            DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_SAVED), COLOR_BLUE);
        }
    } else if (gPage == SETTINGS_PAGE_LANGUAGE) {
        DrawHint(X0, &Y, MaxBottom, LocStr(MSG_SET_PAGE_LANG), COLOR_BLACK);
        Y += 2;
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_LANG_EN),
                      LocaleGet() == LOC_LANG_EN, 1);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_LANG_ZH),
                      LocaleGet() == LOC_LANG_ZH, 2);
        DrawButtonRow(X0, &Y, Bw, Bh, Gap, MaxBottom, LocStr(MSG_SET_HINT_BACK), 0, 0);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

static void ApplyDesktopColor(int Index) {
    if (Index < 0 || Index >= DESKTOP_COLOR_COUNT) {
        return;
    }
    ThemeSetDesktopBg(gDesktopColors[Index].Color);
    ThemeApply();
}

static void ApplyShellColor(int Index) {
    if (Index < 0 || Index >= SHELL_COLOR_COUNT) {
        return;
    }
    ThemeSetShellClientBg(gShellColors[Index].Color);
    ThemeApply();
}

static void ApplyFont(int Index) {
    if (Index < 0 || (UINT32)Index >= FontCount()) {
        return;
    }
    if (ThemeSetFontId((UINT32)Index) != 0) {
        return;
    }
    ThemeApply();
}

static void ApplyDisplayChoice(int Index) {
    if (Index == 0) {
        ThemeClearDisplayMode();
    } else if (Index >= 1 && Index <= MODE_COUNT) {
        ThemeSetDisplayMode(gModes[Index - 1].W, gModes[Index - 1].H);
    } else {
        return;
    }
    if (ThemeSave() != 0) {
        HalConsoleWriteSerial("settings: display save failed\n");
        DebugWrite("settings: display save failed\n");
        gRebootHint = 0;
        PaintMenu();
        return;
    }
    gRebootHint = 1;
    HalConsoleWriteSerial("settings: display saved; QEMU: quit and ./run-split.sh (edid); HW: reboot\n");
    DebugWrite("settings: display pref saved (relaunch QEMU on VM)\n");
    PaintMenu();
}

int SettingsUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_SETTINGS;
}

void SettingsUiRepaint(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
    GuiBackupFocusWindow();
}

void SettingsUiPaintFocused(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
}

void SettingsUiOpen(void) {
    gPage = SETTINGS_PAGE_MAIN;
    gRebootHint = 0;
    PaintMenu();
    DebugWrite("settings: main menu\n");
}

void SettingsUiRefresh(void) {
    int i;

    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) != GUI_WIN_SETTINGS) {
            continue;
        }
        GuiRaiseToFront(i);
        GuiPaintWindow(i);
        PaintMenu();
        return;
    }
}

void SettingsUiOnEscape(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    if (gPage != SETTINGS_PAGE_MAIN) {
        gPage = SETTINGS_PAGE_MAIN;
        PaintMenu();
        return;
    }
    PaintMenu();
}

void SettingsUiOnDigit(char Digit) {
    int N;

    if (!SettingsUiIsFocused()) {
        return;
    }
    if (Digit < '0' || Digit > '9') {
        return;
    }
    N = Digit - '0';

    if (gPage == SETTINGS_PAGE_MAIN) {
        if (N == 1) {
            gPage = SETTINGS_PAGE_DESKTOP_BG;
            PaintMenu();
        } else if (N == 2) {
            gPage = SETTINGS_PAGE_SHELL_BG;
            PaintMenu();
        } else if (N == 3) {
            gPage = SETTINGS_PAGE_FONT;
            PaintMenu();
        } else if (N == 4) {
            gPage = SETTINGS_PAGE_DISPLAY;
            PaintMenu();
        } else if (N == 5) {
            gPage = SETTINGS_PAGE_LANGUAGE;
            PaintMenu();
        }
        return;
    }

    if (N == 0) {
        gPage = SETTINGS_PAGE_MAIN;
        PaintMenu();
        return;
    }

    if (gPage == SETTINGS_PAGE_DESKTOP_BG) {
        ApplyDesktopColor(N - 1);
        return;
    }
    if (gPage == SETTINGS_PAGE_SHELL_BG) {
        ApplyShellColor(N - 1);
        return;
    }
    if (gPage == SETTINGS_PAGE_FONT) {
        ApplyFont(N - 1);
        return;
    }
    if (gPage == SETTINGS_PAGE_DISPLAY) {
        ApplyDisplayChoice(N - 1);
        return;
    }
    if (gPage == SETTINGS_PAGE_LANGUAGE) {
        if (N == 1) {
            (void)LocaleSet(LOC_LANG_EN);
            PaintMenu();
        } else if (N == 2) {
            (void)LocaleSet(LOC_LANG_ZH);
            PaintMenu();
        }
        return;
    }
}

/* PR-G12：客户区点选 → 与数字键同一 Action */
void SettingsUiOnClick(UINT32 X, UINT32 Y) {
    int i;

    if (!SettingsUiIsFocused()) {
        return;
    }
    for (i = 0; i < gHitCount; i++) {
        if (UiHitRect(gHits[i].X, gHits[i].Y, gHits[i].W, gHits[i].H, X, Y)) {
            SettingsUiOnDigit((char)('0' + gHits[i].Action));
            return;
        }
    }
}
