/*
 * SettingsUiApply.c — 点选条目后写 Theme
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

void ApplyDesktopColor(int Index) {
    if (Index < 0 || Index >= DESKTOP_COLOR_COUNT) {
        return;
    }
    ThemeSetDesktopBackground(gDesktopColors[Index].Color);
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
    default:
        break;
    }
    PaintMenu();
}
