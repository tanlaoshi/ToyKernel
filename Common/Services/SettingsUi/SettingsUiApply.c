/*
 * SettingsUiApply.c — 点选条目后写 Theme
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

void ApplyDesktopColor(int Index) {
    if (Index < 0 || Index >= DESKTOP_COLOR_COUNT) {
        return;
    }
    if (gDesktopColors[Index].Color == DESKTOP_COLOR_WALLPAPER) {
        ThemeSetWallpaper(1);
    } else {
        ThemeSetWallpaper(0);
        ThemeSetDesktopBackground(gDesktopColors[Index].Color);
    }
    ThemeApply();
}

void ApplyShellColor(int Index) {
    if (Index < 0 || Index >= SHELL_COLOR_COUNT) {
        return;
    }
    ThemeSetShellClientBackground(gShellColors[Index].Color);
    ThemeApply();
}

void ApplyFont(int Index) {
    if (Index < 0 || (UINT32)Index >= FontCount()) {
        return;
    }
    if (ThemeSetFontId((UINT32)Index) != 0) {
        return;
    }
    ThemeApply();
}

void ApplyScaleChoice(int Index) {
    static int sBusy;

    if (Index < 0 || Index >= SCALE_COUNT) {
        return;
    }
    if (sBusy) {
        return;
    }
    sBusy = 1;
    GuiInputLock(1);
    if (ThemeApplyUiScaleLive(gScales[Index]) == 0) {
        gDisplayHint = 2;
        (void)ThemeSave();
    } else {
        gDisplayHint = 0;
    }
    GuiInputLock(0);
    sBusy = 0;
    PaintMenu();
}

void ApplyDisplayChoice(int Index) {
    UINT32 W = 0;
    UINT32 H = 0;
    int Live = 0;
    int Saved = 0;
    static int sBusy;

    if (sBusy) {
        return;
    }
    sBusy = 1;
    GuiInputLock(1);

    if (Index == 0) {
        ThemeClearDisplayMode();
    } else if (Index >= 1 && Index <= ModeCount()) {
        W = gModes[Index - 1].W;
        H = gModes[Index - 1].H;
        ThemeSetDisplayMode(W, H);
    } else {
        GuiInputLock(0);
        sBusy = 0;
        return;
    }

    if (Index >= 1 && W != 0 && H != 0) {
        char Dim[24];
        FormatUxU(Dim, sizeof(Dim), W, H);
        HalConsoleWriteSerial("settings: apply ");
        HalConsoleWriteSerial(Dim);
        HalConsoleWriteSerial("\n");
        if (ThemeApplyDisplayLive(W, H) == 0) {
            Live = 1;
            HalConsoleWriteSerial("settings: display applied live\n");
            DebugWrite("settings: display applied live\n");
        }
    }

    if (ThemeSave() == 0) {
        Saved = 1;
    } else {
        HalConsoleWriteSerial("settings: display save failed\n");
        DebugWrite("settings: display save failed\n");
    }

    if (Live) {
        gDisplayHint = 2;
        HalConsoleWriteSerial(
            "settings: live OK; cold boot still needs quit QEMU + ./run-split.sh\n");
    } else if (Saved) {
        gDisplayHint = 1;
        if (HalCpuIsHypervisor()) {
            HalConsoleWriteSerial(
                "settings: display saved; quit QEMU window, then ./run-split.sh (edid)\n");
            DebugWrite("settings: display pref saved (relaunch QEMU on VM)\n");
        } else {
            HalConsoleWriteSerial(
                "settings: display pref saved; reboot real PC to apply THEME.CFG\n");
            DebugWrite("settings: display pref saved (real PC reboot)\n");
        }
    } else {
        gDisplayHint = 0;
    }
    GuiInputLock(0);
    sBusy = 0;
    PaintMenu();
}

void ApplyThemeChoice(int Index) {
    if (Index < 0 || Index >= THEME_CHOICE_COUNT) {
        return;
    }
    if (Index == 0) {
        ThemeSetThemeId(THEME_PALETTE_DEFAULT);
        ThemeSetDesktopGradient(0);
    } else if (Index == 1) {
        ThemeSetThemeId(THEME_PALETTE_TECH);
        ThemeSetDesktopGradient(0);
    } else {
        ThemeSetThemeId(THEME_PALETTE_TECH);
        ThemeSetDesktopGradient(1);
    }
    ThemeApply();
}

void ApplyEffectsChoice(int Index) {
    if (Index < 0 || Index >= EFFECTS_CHOICE_COUNT) {
        return;
    }
    ThemeSetEffectLevel((THEME_EFFECT_LEVEL)Index);
    ThemeApply();
}

void ApplyItem(int Idx) {
    if (Idx < 0 || Idx >= ItemCount()) {
        return;
    }
    gItemSel = Idx;
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        ApplyDesktopColor(Idx);
        break;
    case SETTINGS_CAT_SHELL:
        ApplyShellColor(Idx);
        break;
    case SETTINGS_CAT_FONT:
        ApplyFont(Idx);
        break;
    case SETTINGS_CAT_DISPLAY:
        ApplyDisplayChoice(Idx);
        return;
    case SETTINGS_CAT_LANGUAGE:
        (void)LocaleSet(Idx == 0 ? LOC_LANG_EN : LOC_LANG_ZH);
        break;
    case SETTINGS_CAT_SCALE:
        ApplyScaleChoice(Idx);
        return;
    case SETTINGS_CAT_THEME:
        ApplyThemeChoice(Idx);
        break;
    case SETTINGS_CAT_EFFECTS:
        ApplyEffectsChoice(Idx);
        break;
    default:
        break;
    }
    PaintMenu();
}

void FormatNowDisplay(char *Out, UINTN Max) {
    UINT32 PhysW = 0;
    UINT32 PhysH = 0;
    UINT32 LogW = 0;
    UINT32 LogH = 0;
    UINT32 Sc;
    UINTN N = 0;

    if (Max == 0) {
        return;
    }
    HalVideoGetPhysicalSize(&PhysW, &PhysH);
    HalVideoGetSize(&LogW, &LogH);
    if (PhysW == 0 || PhysH == 0) {
        PhysW = LogW;
        PhysH = LogH;
    }
    Out[0] = 'N';
    Out[1] = 'o';
    Out[2] = 'w';
    Out[3] = ' ';
    FormatUxU(Out + 4, Max > 4 ? Max - 4 : 0, PhysW, PhysH);
    while (Out[N]) {
        N++;
    }
    Sc = ThemeUiScale();
    if (N + 12 < Max) {
        Out[N++] = ' ';
        Out[N++] = 's';
        Out[N++] = 'c';
        Out[N++] = 'a';
        Out[N++] = 'l';
        Out[N++] = 'e';
        Out[N++] = '=';
        if (Sc >= 100) {
            Out[N++] = (char)('0' + (Sc / 100) % 10);
        }
        Out[N++] = (char)('0' + (Sc / 10) % 10);
        Out[N++] = (char)('0' + (Sc % 10));
        Out[N++] = '%';
        Out[N] = 0;
    }
    if (Sc != 100 && (LogW != PhysW || LogH != PhysH) && N + 16 < Max) {
        Out[N++] = ' ';
        Out[N++] = 'U';
        Out[N++] = 'I';
        Out[N++] = ' ';
        FormatUxU(Out + N, Max - N, LogW, LogH);
    }
}

void FormatUxU(char *Out, UINTN Max, UINT32 A, UINT32 B) {
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
