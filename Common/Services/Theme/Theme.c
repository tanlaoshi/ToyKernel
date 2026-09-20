/*
 * Theme.c — 主题状态、getter/setter、Apply
 * 热切：ThemeLive.c；解析：ThemeParse.c；CFG/Load：ThemeCfg.c；Save：ThemeSave.c。
 */
#include "Theme.h"
#include "ThemePrivate.h"

UINT32 gDesktopBg = COLOR_DARK_GRAY;
UINT32 gShellClientBg = COLOR_LIGHT_GRAY;
UINT32 gFontId;
UINT32 gModeW;
UINT32 gModeH;
UINT32 gThemeUiScale = 100; /* 50 / 100 / 150 / 200 */
UINT32 gFadeSteps = 6;      /* PR-GUI-l3-fade；0=关 */
int gWallpaper = 1;         /* 默认 WALL.BMP；Settings 选色后关 */
int gThemeId = THEME_PALETTE_DEFAULT;
int gDesktopGrad = 0;       /* tech 对角渐变；默认关 */

void ThemeInitialize(void) {
    gDesktopBg = COLOR_DARK_GRAY;
    gShellClientBg = COLOR_LIGHT_GRAY;
    /* 默认小字：16×32 会撑爆 Store 等窄按钮；有 Sun 8x16 则用它 */
    gFontId = 2; /* Terminus 10x18；ThemeLoad 后再选 Sun */
    gModeW = 0;
    gModeH = 0;
    gThemeUiScale = 100;
    gFadeSteps = 6;
    gWallpaper = 1;
    gThemeId = THEME_PALETTE_DEFAULT;
    gDesktopGrad = 0;
    /*
     * boot GOP 镜像中：勿套桌面默认 10x18（4K 写不满一屏）。
     * gFontId 仍记桌面偏好；进调度前 KernelMain 再 FontSetById(ThemeFontId())。
     */
    if (HalSerialGopMirroring()) {
        HalSerialBootFontApply();
    } else {
        (void)FontSetById(gFontId);
    }
}

UINT32 ThemeDesktopBackground(void) {
    return gDesktopBg;
}

int ThemeWallpaperEnabled(void) {
    if (gThemeId == THEME_PALETTE_TECH) {
        return 0;
    }
    return gWallpaper != 0;
}

void ThemeSetWallpaper(int Enabled) {
    gWallpaper = Enabled ? 1 : 0;
}

int ThemeThemeId(void) {
    return gThemeId;
}

void ThemeSetThemeId(int Id) {
    if (Id != THEME_PALETTE_TECH) {
        Id = THEME_PALETTE_DEFAULT;
    }
    gThemeId = Id;
    if (Id == THEME_PALETTE_TECH) {
        ThemeTechApplyDefaults();
    } else {
        gDesktopBg = COLOR_DARK_GRAY;
        gShellClientBg = COLOR_LIGHT_GRAY;
        gWallpaper = 1;
        gDesktopGrad = 0;
    }
}

int ThemeDesktopGradientEnabled(void) {
    return (gThemeId == THEME_PALETTE_TECH && gDesktopGrad != 0) ? 1 : 0;
}

void ThemeSetDesktopGradient(int Enabled) {
    gDesktopGrad = Enabled ? 1 : 0;
    if (gDesktopGrad && gThemeId != THEME_PALETTE_TECH) {
        gDesktopGrad = 0;
    }
}

UINT32 ThemeShellClientBackground(void) {
    return gShellClientBg;
}

UINT32 ThemeFontId(void) {
    return gFontId;
}

UINT32 ThemeClientPadding(void) {
    return 8u; /* 与 GUI_CLIENT_PAD 对齐 */
}

UINT8 ThemeTaskbarAlpha(void) {
    return 200u; /* ~78%：壁纸隐约透出 */
}

UINT8 ThemeMenuPanelAlpha(void) {
    /* 不透明：半透会叠进窗备份 → 关菜单后「烙印」 */
    return 255u;
}

UINT32 ThemeWindowShadowSize(void) {
    return 6u; /* 窗外 N 像素 */
}

UINT8 ThemeWindowShadowMaxAlpha(void) {
    return 128u; /* 贴边约 50%，外缘收到 0 */
}

UINT32 ThemeWindowTitleGradientBottom(UINT32 Top) {
    /* 向黑插值：保留约 60% 顶色 → 标题栏自上而下略暗 */
    return UiBlendRgb(COLOR_BLACK, Top, 153u);
}

UINT32 ThemeWindowFadeSteps(void) {
    return gFadeSteps;
}

void ThemeSetWindowFadeSteps(UINT32 Steps) {
    if (Steps > 16u) {
        Steps = 16u;
    }
    gFadeSteps = Steps;
}

/* 优先 Sun 8x16，其次 Terminus 10x18；避免默认 16×32 */
UINT32 ThemeCompactFontId(void) {
    UINT32 i;
    const FONT_FACE *F;
    UINT32 Fallback = 2;

    if (FontCount() == 0) {
        return 0;
    }
    if (Fallback >= FontCount()) {
        Fallback = FontCount() - 1;
    }
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        if (!F || !F->Name) {
            continue;
        }
        if (F->Width <= 8 && F->Height <= 16) {
            return i; /* Sun 8x16 等 */
        }
    }
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        if (!F || !F->Name) {
            continue;
        }
        if (F->Width <= 10 && F->Height <= 18) {
            return i;
        }
    }
    return Fallback;
}

UINT32 ThemeDisplayWidth(void) {
    return gModeW;
}

UINT32 ThemeDisplayHeight(void) {
    return gModeH;
}

int ThemeHasDisplayPref(void) {
    return gModeW >= 640 && gModeH >= 480;
}

void ThemeSetDisplayMode(UINT32 Width, UINT32 Height) {
    gModeW = Width;
    gModeH = Height;
}

void ThemeClearDisplayMode(void) {
    gModeW = 0;
    gModeH = 0;
}

UINT32 ThemeUiScale(void) {
    return NormalizeUiScale(gThemeUiScale);
}

void ThemeSetUiScale(UINT32 Percent) {
    gThemeUiScale = NormalizeUiScale(Percent);
}

void ThemeSetDesktopBackground(UINT32 Color) {
    gDesktopBg = Color;
}

void ThemeSetShellClientBackground(UINT32 Color) {
    gShellClientBg = Color;
}

int ThemeSetFontId(UINT32 Id) {
    UINT32 Prev = gFontId;

    if (FontSetById(Id) != 0) {
        (void)FontSetById(Prev);
        return -1;
    }
    gFontId = Id;
    return 0;
}

void ThemeApply(void) {
    if (gFontId >= FontCount()) {
        gFontId = FontCurrentId() < FontCount() ? FontCurrentId() : 0;
    }
    if (FontSetById(gFontId) != 0) {
        gFontId = 0;
        (void)FontSetById(0);
    }
    GuiApplyThemeColors();
    /* PR-G8：属性已更新 → 一次自下而上合成 → 备份；勿 GuiRedraw+Raise 多遍 */
    GuiComposeThemeScene();
    (void)ThemeSave();
}

/* FontReloadAssets 后：偏好 id 失效则改选紧凑字体（Sun 8x16 / 10x18），勿掉到 16×32 */
void ThemeClampFontId(void) {
    if (gFontId >= FontCount() || FontSetById(gFontId) != 0) {
        gFontId = ThemeCompactFontId();
        (void)FontSetById(gFontId);
    }
}
