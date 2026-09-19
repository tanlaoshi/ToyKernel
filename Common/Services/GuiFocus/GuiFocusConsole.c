/*
 * GuiFocusConsole.c — Shell 输入行与是否接受输入
 * 核心：GuiFocus.c
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"

void GuiConsolePull(char *Line, int *Len, int *WaitPrompt) {
    GUI_WINDOW *Win;

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
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
    Win = &gWindows[gFocusWin];
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

    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return;
    }
    Win = &gWindows[gFocusWin];
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
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return 0;
    }
    return !gWindows[gFocusWin].PromptShown;
}


void GuiConsoleMarkPrompt(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return;
    }
    gWindows[gFocusWin].PromptShown = 1;
}


void GuiShellRequestPrompt(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_SHELL) {
            gWindows[i].PromptShown = 0;
        }
    }
}


int GuiConsoleHasDisplay(void) {
    if (gFocusWin < 0 || gFocusWin >= MAX_WINS || !gWindows[gFocusWin].Active) {
        return 0;
    }
    return gWindows[gFocusWin].TermSet;
}

int GuiShellAcceptsInput(void) {
    return gFocusWin >= 0 && gFocusWin < MAX_WINS &&
           gWindows[gFocusWin].Active &&
           gWindows[gFocusWin].Kind == GUI_WIN_SHELL &&
           !WindowOccludedByOther(gFocusWin);
}


int GuiShellWindowActive(int Idx) {
    return Idx >= 0 && Idx < MAX_WINS && gWindows[Idx].Active &&
           gWindows[Idx].Kind == GUI_WIN_SHELL;
}
