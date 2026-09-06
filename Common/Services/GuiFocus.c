/*
 * GuiFocus.c — PR-R2：焦点窗 / Shell 输入行状态
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"

int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background) {
    const GUI_WINDOW *Win;
    UINT32 Pad = GUI_CLIENT_PAD;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    Win = &gWins[gFocusWin];
    /* 与 DrawWindowAt 一致：1px 白边内侧再加 GUI_CLIENT_PAD 文本边距 */
    if (X) {
        *X = Win->X + 1 + Pad;
    }
    if (Y) {
        *Y = Win->Y + TITLE_HEIGHT + Pad;
    }
    if (Width) {
        *Width = (Win->Width > 2 + Pad * 2) ? Win->Width - 2 - Pad * 2 : 0;
    }
    if (Height) {
        *Height = (Win->Height > TITLE_HEIGHT + 1 + Pad * 2) ?
             Win->Height - TITLE_HEIGHT - 1 - Pad * 2 : 0;
    }
    if (Background) {
        *Background = Win->Background;
    }
    if (Width && Height) {
        return *Width > 0 && *Height > 0;
    }
    return 1;
}


void GuiFocusSave(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    GuiConsoleOpsFocusSave();
    Win = &gWins[gFocusWin];
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
}


void GuiConsolePull(char *Line, int *Len, int *WaitPrompt) {
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        if (Len) {
            *Len = 0;
        }
        if (WaitPrompt) {
            *WaitPrompt = 0;
        }
        if (Line) {
            Line[0] = 0;
        }
        return;
    }
    Win = &gWins[gFocusWin];
    if (Line) {
        int i;
        for (i = 0; i < Win->InputLen && i < GUI_INPUT_LINE_MAX - 1; i++) {
            Line[i] = Win->InputLine[i];
        }
        Line[i] = 0;
    }
    if (Len) {
        *Len = Win->InputLen;
    }
    if (WaitPrompt) {
        *WaitPrompt = Win->WaitPrompt;
    }
}


void GuiConsolePush(const char *Line, int Len, int WaitPrompt) {
    GUI_WINDOW *Win;
    int i;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    Win = &gWins[gFocusWin];
    if (Len >= GUI_INPUT_LINE_MAX) {
        Len = GUI_INPUT_LINE_MAX - 1;
    }
    Win->InputLen = Len;
    Win->WaitPrompt = WaitPrompt;
    for (i = 0; i < Len; i++) {
        Win->InputLine[i] = Line[i];
    }
    Win->InputLine[Len] = 0;
}


int GuiConsoleNeedsPrompt(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    return !gWins[gFocusWin].PromptShown;
}


void GuiConsoleMarkPrompt(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    gWins[gFocusWin].PromptShown = 1;
}


void GuiShellRequestPrompt(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWins[i].Active && gWins[i].Kind == GUI_WIN_SHELL) {
            gWins[i].PromptShown = 0;
        }
    }
}


int GuiConsoleHasDisplay(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return 0;
    }
    return gWins[gFocusWin].TermSet;
}


void GuiFocusApply(void) {
    GuiFocusApplyClip();
    GuiConsoleOpsFocusLoad();
}


void GuiFocusApplyClip(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;
    UINT32 Tx;
    UINT32 Ty;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipRegion(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    if (!Win->TermSet) {
        HalVideoSetTextCursor(X, Y);
        Win->TermX = 0;
        Win->TermY = 0;
        return;
    }
    Tx = X + Win->TermX;
    Ty = Y + Win->TermY;
    if (Tx < X) {
        Tx = X;
    }
    if (Ty < Y) {
        Ty = Y;
    }
    if (W > 0 && Tx >= X + W) {
        Tx = X + W - 1;
    }
    if (H > 0 && Ty >= Y + H) {
        Ty = Y + H - 1;
    }
    HalVideoSetTextCursor(Tx, Ty);
    Win->TermX = Tx - X;
    Win->TermY = Ty - Y;
}


void GuiFocusSyncCursor(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Ax;
    UINT32 Ay;
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWins[gFocusWin].Active) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &W, &H, &Bg)) {
        return;
    }
    Win = &gWins[gFocusWin];
    HalVideoGetTextCursor(&Ax, &Ay);
    Win->TermX = (Ax >= Cx) ? (Ax - Cx) : 0;
    Win->TermY = (Ay >= Cy) ? (Ay - Cy) : 0;
    if (W > 0 && Win->TermX >= W) {
        Win->TermX = W - 1;
    }
    if (H > 0 && Win->TermY >= H) {
        Win->TermY = H - 1;
    }
    Win->TermSet = 1;
}


void GuiFocusClearClient(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    /*
     * 整块清客户区。主题合成靠 gDeferPresent + 上层后画，勿 FillRectOccluded，
     * 否则重叠区不换色，抬窗后备份镂空。
     */
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    Win = &gWins[gFocusWin];
    Win->TermX = 0;
    Win->TermY = 0;
    Win->TermSet = 1;
    GuiBackupSyncRect(X, Y, W, H);
    CursorPaint();
    GfxPresent();
    GfxIrqLeave();
}


int GuiShellAcceptsInput(void) {
    return gFocusWin >= 0 && gFocusWin < MAX_WINS &&
           gWins[gFocusWin].Active &&
           gWins[gFocusWin].Kind == GUI_WIN_SHELL &&
           !WindowOccludedByOther(gFocusWin);
}


int GuiShellWindowActive(int Idx) {
    return Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active &&
           gWins[Idx].Kind == GUI_WIN_SHELL;
}


void GuiSetFocusWin(int Idx) {
    if (Idx >= 0 && Idx < MAX_WINS && gWins[Idx].Active) {
        gFocusWin = Idx;
    }
}


int GuiFocusIndex(void) {
    return gFocusWin;
}


void GuiFocusHome(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    GUI_WINDOW *Win;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        HalVideoClearClip();
        return;
    }
    HalVideoSetClipOrigin(X, Y, W, H, Bg);
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS) {
        Win = &gWins[gFocusWin];
        Win->TermX = 0;
        Win->TermY = 0;
        Win->TermSet = 1;
    }
}

