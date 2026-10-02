/*
 * GuiOpenApps.c — Shell / Settings / Store / Files / Edit 开窗
 * 核心：GuiOpen.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"
#include "Debug.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Locale.h"

int GuiOpenShell(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    PlaceNewWindow(Idx, &X, &Y, &W, &H);
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_SHELL;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeShellClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_SHELL);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    /* 先标已 prompt，避免 FocusApply→FocusLoad 抢画；下方 OnShellOpened 再画欢迎语 */
    gWindows[Idx].PromptShown = 1;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    GuiFocusSave();
    GuiConsoleOpsOnShellOpened();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open shell idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenSettings(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = FocusExistingKind(GUI_WIN_SETTINGS, SettingsUiRepaint, "settings");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    /* 三分栏：与 Files 同量级，靠右 */
    W = 800;
    H = 560;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = (gScreenWidth > W + Margin) ? (gScreenWidth - W - Margin) : Margin;
    Y = Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_SETTINGS;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_SETTINGS);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    SettingsUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open settings idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenStore(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = FocusExistingKind(GUI_WIN_STORE, StoreUiRepaint, "store");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 920;
    H = 600;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin;
    Y = Margin + 24;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_STORE;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_STORE);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    StoreUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open store idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenFiles(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 800;
    H = 560;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin;
    Y = (gScreenHeight > H + Margin) ? (gScreenHeight - H - Margin) : Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_FILES;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_FILES);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    FilesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open files idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenEdit(const char *Path) {
    int Idx;
    int Existing;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 56;

    if (!Path || !Path[0]) {
        return -1;
    }

    /* Edit 亦单例：复用已有窗并换文件 */
    Existing = FocusExistingKind(GUI_WIN_EDIT, 0, "edit");
    if (Existing >= 0) {
        EditUiOpen(Path);
        DrawWindowAt(Existing);
        EditUiRepaint();
        BackupWindowAt(Existing);
        GuiFocusApply();
        BackupWindowAt(gFocusWin);
        return gFocusWin;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 800;
    H = 520;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin + 24;
    Y = Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_EDIT;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = "Edit";
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    EditUiOpen(Path);
    EditUiRepaint();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open edit idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}
