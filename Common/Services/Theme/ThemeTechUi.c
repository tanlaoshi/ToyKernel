/*
 * ThemeTechUi.c — PR-GUI-tech-2：文字 / 图标 / 菜单 / 面板 / 滚动条 getter
 *
 * classic 返回值与替换前硬编码一致；tech 取自色板表。
 */
#include "Theme.h"
#include "ThemePrivate.h"

#define TECH_SHELL_TEXT     0x00C0E0E8u
#define TECH_SHELL_PROMPT   0x0000D0FFu
#define TECH_ICON_TEXT      0x00E0E8F0u
#define TECH_ICON_BORDER    0x00304050u
#define TECH_ICON_SELECT    0x0000D0FFu
#define TECH_CLOCK          0x0080D0E0u
#define TECH_START_TEXT     0x00E0E8F0u
#define TECH_MENU_BORDER    0x0000A0C0u
#define TECH_MENU_TEXT      0x00E0E8F0u
#define TECH_MENU_SEP       0x00304058u
#define TECH_FB_POWER       0x00C04040u
#define TECH_FB_REBOOT      0x00C08020u
#define TECH_TEXT           0x00E0E8F0u
#define TECH_TEXT_MUTED     0x00607080u
#define TECH_TEXT_ACCENT    0x0000D0FFu
#define TECH_TEXT_ON_ACCENT 0x00001018u
#define TECH_PANEL_SIDE     0x00182028u
#define TECH_PANEL_DETAIL   0x00141820u
#define TECH_PANEL_SEP      0x00304058u
#define TECH_SCROLL_TRACK   0x00101820u
#define TECH_SCROLL_BORDER  0x00304050u
#define TECH_SCROLL_THUMB   0x0000A0C0u
#define TECH_DIALOG_FACE    0x001C2838u
#define TECH_DIALOG_BORDER  0x0000A0C0u

/* 背景偏亮时用深色字，避免 White/Cyan/Yellow 客户区上提示符「消失」 */
static int ShellBgIsLight(void) {
    UINT32 C = gShellClientBg & 0x00FFFFFFu;
    UINT32 R = (C >> 16) & 0xFFu;
    UINT32 G = (C >> 8) & 0xFFu;
    UINT32 B = C & 0xFFu;

    return ((R * 299u + G * 587u + B * 114u) / 1000u) >= 140u;
}

UINT32 ThemeShellText(void) {
    if (ShellBgIsLight()) {
        return COLOR_BLACK;
    }
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SHELL_TEXT;
    }
    return COLOR_WHITE;
}

UINT32 ThemeShellPrompt(void) {
    if (ShellBgIsLight()) {
        return COLOR_BLUE; /* 与黑字区分 */
    }
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SHELL_PROMPT;
    }
    return COLOR_CYAN;
}

UINT32 ThemeIconText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_ICON_TEXT;
    }
    return COLOR_WHITE;
}

UINT32 ThemeIconBorder(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_ICON_BORDER;
    }
    return COLOR_WHITE;
}

UINT32 ThemeIconSelect(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_ICON_SELECT;
    }
    return COLOR_YELLOW;
}

UINT32 ThemeClockText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_CLOCK;
    }
    return COLOR_WHITE;
}

UINT32 ThemeStartButtonText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_START_TEXT;
    }
    return COLOR_BLACK;
}

UINT32 ThemeMenuBorder(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_MENU_BORDER;
    }
    return COLOR_BLACK;
}

UINT32 ThemeMenuText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_MENU_TEXT;
    }
    return COLOR_BLACK;
}

UINT32 ThemeMenuSep(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_MENU_SEP;
    }
    return ThemeWindowBorderIdle();
}

UINT32 ThemeIconFallbackPower(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_FB_POWER;
    }
    return 0x00C04040u;
}

UINT32 ThemeIconFallbackReboot(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_FB_REBOOT;
    }
    return 0x00C08020u;
}

UINT32 ThemeText(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TEXT;
    }
    return COLOR_BLACK;
}

UINT32 ThemeTextMuted(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TEXT_MUTED;
    }
    return COLOR_DARK_GRAY;
}

UINT32 ThemeTextAccent(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TEXT_ACCENT;
    }
    return COLOR_BLUE;
}

UINT32 ThemeTextOnAccent(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_TEXT_ON_ACCENT;
    }
    return COLOR_WHITE;
}

UINT32 ThemePanelSideBackground(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_PANEL_SIDE;
    }
    return 0x00A0A8B0u;
}

UINT32 ThemePanelDetailBackground(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_PANEL_DETAIL;
    }
    return 0x00D8D8E0u;
}

UINT32 ThemePanelSeparator(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_PANEL_SEP;
    }
    return COLOR_DARK_GRAY;
}

UINT32 ThemeScrollTrack(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SCROLL_TRACK;
    }
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeScrollBorder(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SCROLL_BORDER;
    }
    return COLOR_GRAY;
}

UINT32 ThemeScrollThumb(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_SCROLL_THUMB;
    }
    return COLOR_DARK_GRAY;
}

UINT32 ThemeDialogFace(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_DIALOG_FACE;
    }
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeDialogBorder(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return TECH_DIALOG_BORDER;
    }
    return COLOR_BLACK;
}

/* §3.1：对角渐变暗端 / 亮端 */
#define TECH_GRAD_DARK   0x000A1018u
#define TECH_GRAD_LIGHT  0x00182030u

UINT32 ThemeDesktopGradientAt(UINT32 X, UINT32 Y, UINT32 Sw, UINT32 Sh) {
    UINT32 Den;
    UINT32 Num;
    UINT8 A;
    UINT32 Dr;
    UINT32 Dg;
    UINT32 Db;
    UINT32 Lr;
    UINT32 Lg;
    UINT32 Lb;
    UINT32 R;
    UINT32 G;
    UINT32 B;

    if (Sw == 0) {
        Sw = 1;
    }
    if (Sh == 0) {
        Sh = 1;
    }
    Den = (Sw - 1) + (Sh - 1);
    if (Den == 0) {
        return TECH_GRAD_DARK;
    }
    Num = X + Y;
    if (Num > Den) {
        Num = Den;
    }
    /* Alpha：亮端权重 0..255 */
    A = (UINT8)((Num * 255u) / Den);
    Dr = (TECH_GRAD_DARK >> 16) & 0xFFu;
    Dg = (TECH_GRAD_DARK >> 8) & 0xFFu;
    Db = TECH_GRAD_DARK & 0xFFu;
    Lr = (TECH_GRAD_LIGHT >> 16) & 0xFFu;
    Lg = (TECH_GRAD_LIGHT >> 8) & 0xFFu;
    Lb = TECH_GRAD_LIGHT & 0xFFu;
    R = (Dr * (255u - A) + Lr * A) / 255u;
    G = (Dg * (255u - A) + Lg * A) / 255u;
    B = (Db * (255u - A) + Lb * A) / 255u;
    return (R << 16) | (G << 8) | B;
}
