/*
 * SettingsUiModel.c — 分类/条目文案与显示模式表
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

const char *CatLabel(SETTINGS_CAT C) {
    /* 三分栏左侧短名（locale 已去掉「N.」与括号说明） */
    switch (C) {
    case SETTINGS_CAT_DESKTOP:  return LocStr(MSG_SET_DESKTOP_BG);
    case SETTINGS_CAT_SHELL:    return LocStr(MSG_SET_SHELL_BG);
    case SETTINGS_CAT_FONT:     return LocStr(MSG_SET_FONT);
    case SETTINGS_CAT_DISPLAY:  return LocStr(MSG_SET_DISPLAY);
    case SETTINGS_CAT_LANGUAGE: return LocStr(MSG_SET_LANGUAGE);
    case SETTINGS_CAT_SCALE:    return LocStr(MSG_SET_SCALE);
    case SETTINGS_CAT_THEME:    return LocStr(MSG_SET_THEME);
    default:                    return "?";
    }
}

void EnsureDisplayModes(void) {
    UINT32 N;
    UINT32 i;
    UINT32 W;
    UINT32 H;
    UINTN L;

    if (gModesReady) {
        return;
    }
    gModesReady = 1;
    gModeCount = 0;
    N = HalVideoModeCount();
    if (N > BOOT_VIDEO_MODE_MAX) {
        N = BOOT_VIDEO_MODE_MAX;
    }
    for (i = 0; i < N; i++) {
        if (HalVideoModeGet(i, &W, &H) != 0 || W < 640 || H < 480) {
            continue;
        }
        FormatUxU(gModeLabels[gModeCount], sizeof(gModeLabels[0]), W, H);
        L = 0;
        while (gModeLabels[gModeCount][L]) {
            L++;
        }
        gModes[gModeCount].Label = gModeLabels[gModeCount];
        gModes[gModeCount].W = W;
        gModes[gModeCount].H = H;
        gModeCount++;
    }
    if (gModeCount == 0) {
        N = (UINT32)(sizeof(gModesFallback) / sizeof(gModesFallback[0]));
        for (i = 0; i < N && gModeCount < BOOT_VIDEO_MODE_MAX; i++) {
            gModes[gModeCount] = gModesFallback[i];
            gModeCount++;
        }
    }
}

int ModeCount(void) {
    EnsureDisplayModes();
    return gModeCount;
}

int ItemCount(void) {
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:  return DESKTOP_COLOR_COUNT;
    case SETTINGS_CAT_SHELL:    return SHELL_COLOR_COUNT;
    case SETTINGS_CAT_FONT:     return (int)FontCount();
    case SETTINGS_CAT_DISPLAY:  return 1 + ModeCount();
    case SETTINGS_CAT_LANGUAGE: return 2;
    case SETTINGS_CAT_SCALE:    return SCALE_COUNT;
    case SETTINGS_CAT_THEME:    return THEME_CHOICE_COUNT;
    default:                    return 0;
    }
}

static void CopyStr(char *Dst, int DstMax, const char *S) {
    int j;

    if (DstMax <= 0) {
        return;
    }
    if (!S) {
        Dst[0] = 0;
        return;
    }
    for (j = 0; S[j] && j < DstMax - 1; j++) {
        Dst[j] = S[j];
    }
    Dst[j] = 0;
}

void ItemLabel(int Idx, char *Out, int OutMax) {
    const FONT_FACE *Face;
    UINT32 Sc;
    int P;

    if (OutMax <= 0) {
        return;
    }
    Out[0] = 0;
    if (Idx < 0 || Idx >= ItemCount()) {
        return;
    }
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        CopyStr(Out, OutMax, gDesktopColors[Idx].Label);
        break;
    case SETTINGS_CAT_SHELL:
        CopyStr(Out, OutMax, gShellColors[Idx].Label);
        break;
    case SETTINGS_CAT_FONT:
        Face = FontGetById((UINT32)Idx);
        CopyStr(Out, OutMax, (Face && Face->Name) ? Face->Name : "?");
        break;
    case SETTINGS_CAT_DISPLAY:
        if (Idx == 0) {
            CopyStr(Out, OutMax, "Auto");
        } else {
            CopyStr(Out, OutMax, gModes[Idx - 1].Label);
        }
        break;
    case SETTINGS_CAT_LANGUAGE:
        CopyStr(Out, OutMax,
                (Idx == 0) ? LocStr(MSG_SET_LANG_EN) : LocStr(MSG_SET_LANG_ZH));
        break;
    case SETTINGS_CAT_SCALE:
        Sc = gScales[Idx];
        P = 0;
        if (Sc >= 100) {
            Out[P++] = (char)('0' + (Sc / 100) % 10);
        }
        Out[P++] = (char)('0' + (Sc / 10) % 10);
        Out[P++] = (char)('0' + (Sc % 10));
        Out[P++] = '%';
        Out[P] = 0;
        break;
    case SETTINGS_CAT_THEME:
        if (Idx == 0) {
            CopyStr(Out, OutMax, LocStr(MSG_SET_THEME_DEFAULT));
        } else if (Idx == 1) {
            CopyStr(Out, OutMax, LocStr(MSG_SET_THEME_TECH));
        } else {
            CopyStr(Out, OutMax, LocStr(MSG_SET_THEME_GRAD));
        }
        break;
    default:
        break;
    }
}

/* 当前已生效值在列表中的下标 */
int CurrentItemIndex(void) {
    int i;
    UINT32 Cur;
    UINT32 PrefW;
    UINT32 PrefH;
    int HasPref;

    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        if (ThemeWallpaperEnabled()) {
            return 0;
        }
        Cur = ThemeDesktopBackground();
        for (i = 1; i < DESKTOP_COLOR_COUNT; i++) {
            if (gDesktopColors[i].Color == Cur) {
                return i;
            }
        }
        return 1;
    case SETTINGS_CAT_SHELL:
        Cur = ThemeShellClientBackground();
        for (i = 0; i < SHELL_COLOR_COUNT; i++) {
            if (gShellColors[i].Color == Cur) {
                return i;
            }
        }
        return 0;
    case SETTINGS_CAT_FONT:
        return (int)ThemeFontId();
    case SETTINGS_CAT_DISPLAY:
        HasPref = ThemeHasDisplayPref();
        if (!HasPref) {
            return 0;
        }
        PrefW = ThemeDisplayWidth();
        PrefH = ThemeDisplayHeight();
        for (i = 0; i < ModeCount(); i++) {
            if (gModes[i].W == PrefW && gModes[i].H == PrefH) {
                return i + 1;
            }
        }
        return 0;
    case SETTINGS_CAT_LANGUAGE:
        return (LocaleGet() == LOC_LANG_ZH) ? 1 : 0;
    case SETTINGS_CAT_SCALE:
        Cur = ThemeUiScale();
        for (i = 0; i < SCALE_COUNT; i++) {
            if (gScales[i] == Cur) {
                return i;
            }
        }
        return 1;
    case SETTINGS_CAT_THEME:
        if (ThemeThemeId() != THEME_PALETTE_TECH) {
            return 0;
        }
        return ThemeDesktopGradientEnabled() ? 2 : 1;
    default:
        return 0;
    }
}
