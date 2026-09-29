/*
 * ConsoleSbPaint.c — Shell scrollback 重画（PR-S3-consolescroll-1）
 *
 * 从 ConsoleScroll.c 原样搬家；不改语义。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "Gui.h"
#include "Font.h"
#include "UI.h"

void ConsoleSbRepaint(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int VisRows;
    int Start;
    int End;
    int MaxOff;
    int i;
    int Count = ConsoleSbLineCount();
    int ViewOff = ConsoleSbViewOff();
    int AccLen = ConsoleSbAccLen();
    const char *Acc = ConsoleSbAcc();

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Ch == 0) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 8) {
        LineH = 16;
    }
    Vis = (int)(Ch / LineH);
    if (Vis < 1) {
        Vis = 1;
    }

    /*
     * 安静清客户区：勿走 GuiFocusClearClient（内含 GfxPresent），
     * 否则滚轮每格 Present 极慢。
     */
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    GuiFocusHome();

    End = Count - ViewOff;
    if (End < 0) {
        End = 0;
    }
    if (End > Count) {
        End = Count;
    }
    VisRows = Vis;
    if (ViewOff == 0 && AccLen > 0) {
        VisRows = Vis - 1;
    }
    if (VisRows < 1) {
        VisRows = 1;
    }
    Start = End - VisRows;
    if (Start < 0) {
        Start = 0;
    }
    MaxOff = ConsoleSbMaxOff(Vis);
    if (MaxOff > 0) {
        ConsoleSbBarPrepare(Cx, Cy, Cw, Ch, Bg, LineH, VisRows, Start, Count);
    } else {
        ConsoleSbBarReset();
        HalVideoSetClipOrigin(Cx, Cy, Cw, Ch, Bg);
    }

    for (i = Start; i < End; i++) {
        const char *L = ConsoleSbLine(i);
        /* 历史行里的提示符也保持青色 */
        if (L[0] == 't' && L[1] == 'o' && L[2] == 'y' && L[3] == 'o' &&
            L[4] == 's' && L[5] == '>' && L[6] == ' ') {
            ConsoleDrawString("toyos> ", ThemeShellPrompt());
            if (L[7]) {
                ConsoleDrawString(L + 7, ThemeShellText());
            }
        } else {
            ConsoleDrawString(L, ThemeShellText());
        }
        ConsoleDrawString("\n", ThemeShellText());
    }
    if (ViewOff == 0 && AccLen > 0) {
        if (Acc[0] == 't' && Acc[1] == 'o' && Acc[2] == 'y' && Acc[3] == 'o' &&
            Acc[4] == 's' && Acc[5] == '>' && Acc[6] == ' ') {
            ConsoleDrawString("toyos> ", ThemeShellPrompt());
            if (Acc[7]) {
                ConsoleDrawString(Acc + 7, ThemeShellText());
            }
        } else {
            ConsoleDrawString(Acc, ThemeShellText());
        }
    }

    if (MaxOff > 0) {
        ConsoleSbBarFinishRepaint(Cx, Cy, Cw, Ch, Bg);
    }
    GuiFocusSave();
    GuiBackupFocusWindow();
}

void ConsoleSbPaint(void) {
    if (!GuiShellAcceptsInput()) {
        return;
    }
    ConsoleSbRepaint();
}

/* 若正在看历史，先回到底部再继续输出/输入 */
void ConsoleSbEnsureLive(void) {
    if (ConsoleSbViewOff() == 0) {
        return;
    }
    ConsoleSbSetViewOff(0, 0);
}
