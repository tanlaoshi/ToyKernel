/*
 * GuiOpenTty.c — 串口会话窗开窗（PR-GUI-tty-win）
 */
#include "GuiPrivate.h"
#include "Theme.h"
#include "TtyUi.h"
#include "Locale.h"
#include "Debug.h"

int GuiOpenTty(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = FocusExistingKind(GUI_WIN_TTY, TtyUiRepaint, "tty");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 720;
    H = 480;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin + 24;
    Y = Margin + 48;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_TTY;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeShellClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_TTY);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    TtyUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open tty idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}
