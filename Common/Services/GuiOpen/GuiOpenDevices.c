/*
 * GuiOpenDevices.c — 设备管理器开窗（PR-DEV-6）
 */
#include "GuiPrivate.h"
#include "Theme.h"
#include "DevicesUi.h"
#include "Locale.h"
#include "Debug.h"

int GuiOpenDevices(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = FocusExistingKind(GUI_WIN_DEVICES, DevicesUiRepaint, "devices");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 720;
    H = 520;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin;
    Y = Margin + 24;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_DEVICES;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_DEVICES);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    DevicesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open devices idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}
