/*
 * ThemeTech.c — PR-GUI-tech-1：tech 色板 + chrome getter 分支
 *
 * classic 返回值与搬出 Theme.c 前一致；tech 为深青黑 / 青蓝强调。
 */
#include "Theme.h"
#include "ThemePrivate.h"

#define TECH_DESKTOP        0x00101820u
#define TECH_SHELL          0x000A1018u
#define TECH_SETTINGS       0x00141820u
#define TECH_TITLE_FOCUS    0x00182838u
#define TECH_TITLE_IDLE     0x00202830u
#define TECH_TITLE_HOVER    0x00203048u
#define TECH_BORDER_FOCUS   0x0000A0C0u
#define TECH_BORDER_IDLE    0x00303848u
#define TECH_BORDER_HOVER   0x0000D0FFu
#define TECH_TITLE_TEXT     0x00E0E8F0u
#define TECH_CLOSE          0x00FF6080u
#define TECH_TASKBAR        0x00182028u
#define TECH_TB_BUTTON      0x00202830u
#define TECH_TB_ACTIVE      0x0000A0C0u
#define TECH_CTRL_FACE      0x00202830u
#define TECH_CTRL_BORDER    0x00304050u
#define TECH_CTRL_ACCENT    0x0000D0FFu
#define TECH_SHADOW         0x00000810u

void ThemeTechApplyDefaults(void) {
    ThemeTechApplyColors();
    gWallpaper = 0;
}

void ThemeTechApplyColors(void) {
    gDesktopBg = TECH_DESKTOP;
    gShellClientBg = TECH_SHELL;
}

int ThemeTechParseName(const char *Val, int *OutId) {
    int I;

    if (!Val || !OutId) {
        return -1;
    }
    while (*Val && (*Val == ' ' || *Val == '\t')) {
        Val++;
    }
    if (Val[0] == 't' && Val[1] == 'e' && Val[2] == 'c' && Val[3] == 'h') {
        I = 4;
        if (Val[I] == 0 || Val[I] == '\n' || Val[I] == ' ' || Val[I] == '\t') {
            *OutId = THEME_PALETTE_TECH;
            return 0;
        }
    }
    if (Val[0] == 'd' && Val[1] == 'e' && Val[2] == 'f' && Val[3] == 'a' &&
        Val[4] == 'u' && Val[5] == 'l' && Val[6] == 't') {
        I = 7;
        if (Val[I] == 0 || Val[I] == '\n' || Val[I] == ' ' || Val[I] == '\t') {
            *OutId = THEME_PALETTE_DEFAULT;
            return 0;
        }
    }
    return -1;
}

const char *ThemeTechName(int Id) {
    if (Id == THEME_PALETTE_TECH) {
        return "tech";
    }
    return "default";
}

UINT32 ThemeSettingsClientBackground(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SETTINGS;
    }
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeWindowTitleFocus(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TITLE_FOCUS;
    }
    return COLOR_BLUE;
}

UINT32 ThemeWindowTitleIdle(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TITLE_IDLE;
    }
    return COLOR_GRAY;
}

UINT32 ThemeWindowTitleHover(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TITLE_HOVER;
    }
    return 0x004060A0u;
}

UINT32 ThemeWindowBorderFocus(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_BORDER_FOCUS;
    }
    return COLOR_WHITE;
}

UINT32 ThemeWindowBorderIdle(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_BORDER_IDLE;
    }
    return 0x00A0A0A0u;
}

UINT32 ThemeWindowBorderHover(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_BORDER_HOVER;
    }
    return 0x00C0D0F0u;
}

UINT32 ThemeWindowTitleText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TITLE_TEXT;
    }
    return COLOR_WHITE;
}

UINT32 ThemeCloseButton(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_CLOSE;
    }
    return COLOR_RED;
}

UINT32 ThemeTaskbarBackground(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TASKBAR;
    }
    return COLOR_DARK_GRAY;
}

UINT32 ThemeTaskbarButton(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TB_BUTTON;
    }
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeTaskbarButtonActive(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TB_ACTIVE;
    }
    return COLOR_BLUE;
}

UINT32 ThemeControlFace(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_CTRL_FACE;
    }
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeControlBorder(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_CTRL_BORDER;
    }
    return COLOR_DARK_GRAY;
}

UINT32 ThemeControlAccent(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_CTRL_ACCENT;
    }
    return COLOR_BLUE;
}

UINT32 ThemeWindowShadowColor(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SHADOW;
    }
    return COLOR_BLACK;
}
