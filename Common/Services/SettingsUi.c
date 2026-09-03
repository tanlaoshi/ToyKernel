/*
 * SettingsUi.c — Settings 一级/二级文字菜单（PR-D5）
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
    SETTINGS_PAGE_FONT
} SETTINGS_PAGE;

typedef struct {
    const char *Label;
    UINT32      Color;
} SETTINGS_COLOR;

static SETTINGS_PAGE gPage = SETTINGS_PAGE_MAIN;

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

#define DESKTOP_COLOR_COUNT \
    ((int)(sizeof(gDesktopColors) / sizeof(gDesktopColors[0])))
#define SHELL_COLOR_COUNT \
    ((int)(sizeof(gShellColors) / sizeof(gShellColors[0])))

static int FocusSettingsWindow(void) {
    int i;

    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        return 1;
    }
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) == GUI_WIN_SETTINGS) {
            GuiSetFocusWin(i);
            return 1;
        }
    }
    return 0;
}

static void DrawLine(UINT32 *X, UINT32 *Y, UINT32 X0, const char *Text,
                     UINT32 Color) {
    HalVideoDrawStringAt(*X, *Y, Text, Color);
    *Y += FontAdvanceY();
    *X = X0;
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
    UINT32 MaxY;
    int i;
    char Line[48];
    const FONT_FACE *Face;
    UINT32 CurColor;
    UINT32 CurFont;

    if (!FocusSettingsWindow()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }

    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    X0 = Cx + 12;
    X = X0;
    Y = Cy + 10;
    MaxY = Cy + Ch - FontCellH();

    DrawLine(&X, &Y, X0, "Settings", COLOR_BLUE);
    DrawLine(&X, &Y, X0, "----------------", COLOR_DARK_GRAY);

    if (gPage == SETTINGS_PAGE_MAIN) {
        DrawLine(&X, &Y, X0, "[Main]", COLOR_BLACK);
        DrawLine(&X, &Y, X0, " 1. Desktop background", COLOR_BLACK);
        DrawLine(&X, &Y, X0, " 2. Shell background", COLOR_BLACK);
        DrawLine(&X, &Y, X0, " 3. Font", COLOR_BLACK);
        DrawLine(&X, &Y, X0, "", COLOR_BLACK);
        DrawLine(&X, &Y, X0, "Keys: 1-3 open  Esc back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_DESKTOP_BG) {
        CurColor = ThemeDesktopBg();
        DrawLine(&X, &Y, X0, "[Desktop background]", COLOR_BLACK);
        for (i = 0; i < DESKTOP_COLOR_COUNT && Y <= MaxY; i++) {
            Line[0] = (gDesktopColors[i].Color == CurColor) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            {
                int j;
                const char *S = gDesktopColors[i].Label;
                for (j = 0; S[j] && j < 40; j++) {
                    Line[5 + j] = S[j];
                }
                Line[5 + j] = 0;
            }
            DrawLine(&X, &Y, X0, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, " 0. Back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_SHELL_BG) {
        CurColor = ThemeShellClientBg();
        DrawLine(&X, &Y, X0, "[Shell background]", COLOR_BLACK);
        for (i = 0; i < SHELL_COLOR_COUNT && Y <= MaxY; i++) {
            Line[0] = (gShellColors[i].Color == CurColor) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            {
                int j;
                const char *S = gShellColors[i].Label;
                for (j = 0; S[j] && j < 40; j++) {
                    Line[5 + j] = S[j];
                }
                Line[5 + j] = 0;
            }
            DrawLine(&X, &Y, X0, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, " 0. Back", COLOR_DARK_GRAY);
    } else if (gPage == SETTINGS_PAGE_FONT) {
        CurFont = ThemeFontId();
        DrawLine(&X, &Y, X0, "[Font]", COLOR_BLACK);
        for (i = 0; (UINT32)i < FontCount() && Y <= MaxY; i++) {
            Face = FontGetById((UINT32)i);
            Line[0] = ((UINT32)i == CurFont) ? '*' : ' ';
            Line[1] = ' ';
            Line[2] = (char)('1' + i);
            Line[3] = '.';
            Line[4] = ' ';
            {
                int j;
                const char *S = (Face && Face->Name) ? Face->Name : "?";
                for (j = 0; S[j] && j < 40; j++) {
                    Line[5 + j] = S[j];
                }
                Line[5 + j] = 0;
            }
            DrawLine(&X, &Y, X0, Line, COLOR_BLACK);
        }
        DrawLine(&X, &Y, X0, " 0. Back", COLOR_DARK_GRAY);
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

void SettingsUiOpen(void) {
    gPage = SETTINGS_PAGE_MAIN;
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
         * Shell 重画可能穿透 Settings：必须整窗重画（标题栏+客户区），
         * 再画菜单，避免灰块残留/标题栏被截断。
         */
        GuiSetFocusWin(i);
        GuiPaintWindow(i);
        PaintMenu();
        return;
    }
}

int SettingsUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_SETTINGS;
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
}
