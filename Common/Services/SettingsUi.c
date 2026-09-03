/*
 * SettingsUi.c — Settings 一级/二级文字菜单（PR-D5）+ 分辨率（PR-D7）
 *
 * 分辨率只写 THEME.CFG mode=WxH。
 * QEMU：退出后 ./run.sh（edid）；真机：Guest reboot。勿依赖 Guest reboot 在 QEMU 上换分辨率。
 */
#include "SettingsUi.h"
#include "Gui.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Debug.h"

typedef enum {
    SETTINGS_PAGE_MAIN = 0,
    SETTINGS_PAGE_DESKTOP_BG,
    SETTINGS_PAGE_SHELL_BG,
    SETTINGS_PAGE_FONT,
    SETTINGS_PAGE_DISPLAY
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

static SETTINGS_PAGE gPage = SETTINGS_PAGE_MAIN;
static int gRebootHint;

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
        /* 已是焦点则不再 Raise，避免 SettingsUiRepaint→PaintMenu→Raise 递归 */
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

static void DrawLine(UINT32 *X, UINT32 *Y, UINT32 X0, UINT32 MaxBottom,
                     const char *Text, UINT32 Color) {
    /* 禁止画到客户区外（否则叠在下层 Shell / 桌面上） */
    if (*Y + FontCellH() > MaxBottom) {
        return;
    }
    HalVideoDrawStringAt(*X, *Y, Text, Color);
    *Y += FontAdvanceY();
    *X = X0;
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

static void PaintMenu(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 X;
    UINT32 Y;
    UINT32 X0;
    UINT32 MaxBottom;
    int i;
    char Line[56];
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

    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    X0 = Cx + 12;
    X = X0;
    Y = Cy + 8;
    MaxBottom = Cy + Ch;

    DrawLine(&X, &Y, X0, MaxBottom, "Settings", COLOR_BLUE);
    DrawLine(&X, &Y, X0, MaxBottom, "----------------", COLOR_DARK_GRAY);

    if (gPage == SETTINGS_PAGE_MAIN) {
        HalVideoGetSize(&NowW, &NowH);
        DrawLine(&X, &Y, X0, MaxBottom, "[Main]", COLOR_BLACK);
        DrawLine(&X, &Y, X0, MaxBottom, " 1. Desktop bg", COLOR_BLACK);
        DrawLine(&X, &Y, X0, MaxBottom, " 2. Shell bg", COLOR_BLACK);
        DrawLine(&X, &Y, X0, MaxBottom, " 3. Font", COLOR_BLACK);
        DrawLine(&X, &Y, X0, MaxBottom, " 4. Display (reboot)", COLOR_BLACK);
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
        DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_DARK_GRAY);
        DrawLine(&X, &Y, X0, MaxBottom, "1-4 open  Esc/0 back", COLOR_DARK_GRAY);
        if (gRebootHint) {
            DrawLine(&X, &Y, X0, MaxBottom, "Saved. QEMU: quit+./run.sh", COLOR_BLUE);
        }
    } else if (gPage == SETTINGS_PAGE_DESKTOP_BG) {
        CurColor = ThemeDesktopBg();
        DrawLine(&X, &Y, X0, MaxBottom, "[Desktop bg]", COLOR_BLACK);
        for (i = 0; i < DESKTOP_COLOR_COUNT; i++) {
            Line[0] = (gDesktopColors[i].Color == CurColor) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            CopyLabel(Line + 5, (int)sizeof(Line) - 5, gDesktopColors[i].Label);
            DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, MaxBottom, " 0. Back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_SHELL_BG) {
        CurColor = ThemeShellClientBg();
        DrawLine(&X, &Y, X0, MaxBottom, "[Shell bg]", COLOR_BLACK);
        for (i = 0; i < SHELL_COLOR_COUNT; i++) {
            Line[0] = (gShellColors[i].Color == CurColor) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            CopyLabel(Line + 5, (int)sizeof(Line) - 5, gShellColors[i].Label);
            DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, MaxBottom, " 0. Back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_FONT) {
        CurFont = ThemeFontId();
        DrawLine(&X, &Y, X0, MaxBottom, "[Font]", COLOR_BLACK);
        for (i = 0; (UINT32)i < FontCount(); i++) {
            Face = FontGetById((UINT32)i);
            Line[0] = ((UINT32)i == CurFont) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            CopyLabel(Line + 5, (int)sizeof(Line) - 5,
                      (Face && Face->Name) ? Face->Name : "?");
            DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, MaxBottom, " 0. Back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_DISPLAY) {
        HasPref = ThemeHasDisplayPref();
        PrefW = ThemeDisplayWidth();
        PrefH = ThemeDisplayHeight();
        HalVideoGetSize(&NowW, &NowH);
        DrawLine(&X, &Y, X0, MaxBottom, "[Display] reboot", COLOR_BLACK);
        Line[0] = (!HasPref) ? '*' : ' ';
        Line[1] = ' ';
        Line[2] = '1';
        Line[3] = '.';
        Line[4] = ' ';
        CopyLabel(Line + 5, (int)sizeof(Line) - 5, "Auto");
        DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_BLACK);
        for (i = 0; i < MODE_COUNT; i++) {
            int Mark = HasPref && gModes[i].W == PrefW && gModes[i].H == PrefH;
            Line[0] = Mark ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('2' + i);
            Line[3] = '.';
            Line[4] = ' ';
            CopyLabel(Line + 5, (int)sizeof(Line) - 5, gModes[i].Label);
            DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_BLACK);
        }
        Line[0] = 'N';
        Line[1] = 'o';
        Line[2] = 'w';
        Line[3] = ' ';
        FormatUxU(Line + 4, sizeof(Line) - 4, NowW, NowH);
        DrawLine(&X, &Y, X0, MaxBottom, Line, COLOR_DARK_GRAY);
        DrawLine(&X, &Y, X0, MaxBottom, " 0. Back", COLOR_DARK_GRAY);
        if (gRebootHint) {
            DrawLine(&X, &Y, X0, MaxBottom, "Saved. QEMU: quit+./run.sh", COLOR_BLUE);
        }
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
    /* Index 0 = Auto；1..MODE_COUNT = 预置 */
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
    HalConsoleWriteSerial("settings: display saved; QEMU: quit and ./run.sh (edid); HW: reboot\n");
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

/* PR-G8：焦点已在 Settings 时只画菜单，不 Raise（主题一次合成） */
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
        /*
         * 必须先置顶再画：否则菜单字写在上层 Shell 上，随后被抓进 Shell 备份成「印字」。
         */
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
    /* 一级再 Esc：保持主菜单（关窗用标题栏 ×） */
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
        /* 1=Auto，2..=预置 → ApplyDisplayChoice(N-1) */
        ApplyDisplayChoice(N - 1);
        return;
    }
}
