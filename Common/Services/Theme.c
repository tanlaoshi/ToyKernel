/*
 * Theme.c — 主题存储（PR-D2）
 */
#include "Theme.h"
#include "UI.h"
#include "Font.h"
#include "Gui.h"

static UINT32 gDesktopBg = COLOR_DARK_GRAY;
static UINT32 gShellClientBg = COLOR_LIGHT_GRAY;
static UINT32 gFontId;

void ThemeInit(void) {
    gDesktopBg = COLOR_DARK_GRAY;
    gShellClientBg = COLOR_LIGHT_GRAY;
    gFontId = 0;
    (void)FontSetById(gFontId);
}

UINT32 ThemeDesktopBg(void) {
    return gDesktopBg;
}

UINT32 ThemeShellClientBg(void) {
    return gShellClientBg;
}

UINT32 ThemeFontId(void) {
    return gFontId;
}

void ThemeSetDesktopBg(UINT32 Color) {
    gDesktopBg = Color;
}

void ThemeSetShellClientBg(UINT32 Color) {
    gShellClientBg = Color;
}

int ThemeSetFontId(UINT32 Id) {
    if (FontSetById(Id) != 0) {
        return -1;
    }
    gFontId = Id;
    return 0;
}

void ThemeApply(void) {
    (void)FontSetById(gFontId);
    GuiApplyThemeColors();
}
