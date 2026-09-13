/*
 * ConsoleScroll.c — PR-S-console-split-1：Shell 行缓冲 / 历史 / Paint / 滚轮
 *
 * 从 Console.c 原样搬家；不改语义。绘制经 ConsoleDraw*（Console.c）。
 */
#include "Console.h"
#include "ConsolePriv.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "UI.h"

static char gSb[SB_LINES][SB_COLS];
static int gSbCount;
static int gSbNext;
static int gViewOff;
static char gAcc[SB_COLS];
static int gAccLen;

void ConsoleSbReset(void) {
    gSbCount = 0;
    gSbNext = 0;
    gViewOff = 0;
    gAccLen = 0;
    gAcc[0] = 0;
}

void ConsoleSbPushLine(void) {
    int i;

    for (i = 0; i < SB_COLS - 1 && i < gAccLen; i++) {
        gSb[gSbNext][i] = gAcc[i];
    }
    gSb[gSbNext][i] = 0;
    gSbNext = (gSbNext + 1) % SB_LINES;
    if (gSbCount < SB_LINES) {
        gSbCount++;
    }
    gAccLen = 0;
    gAcc[0] = 0;
}

void ConsoleSbFeedChar(char C) {
    if (C == '\n') {
        ConsoleSbPushLine();
        return;
    }
    if (C < 32 || C == 127) {
        return;
    }
    if (gAccLen + 1 >= SB_COLS) {
        ConsoleSbPushLine();
    }
    if (gAccLen + 1 < SB_COLS) {
        gAcc[gAccLen++] = C;
        gAcc[gAccLen] = 0;
    }
}

void ConsoleSbFeed(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text) {
        ConsoleSbFeedChar(*Text++);
    }
}

void ConsoleSbBackspace(void) {
    if (gAccLen > 0) {
        gAccLen--;
        gAcc[gAccLen] = 0;
    }
}

static const char *ConsoleSbLine(int OldestIndex) {
    int Idx;

    if (OldestIndex < 0 || OldestIndex >= gSbCount) {
        return "";
    }
    Idx = gSbNext - gSbCount + OldestIndex;
    while (Idx < 0) {
        Idx += SB_LINES;
    }
    Idx %= SB_LINES;
    return gSb[Idx];
}

void ConsoleSbPaint(void) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int Start;
    int End;
    int i;

    if (!GuiShellAcceptsInput()) {
        return;
    }
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

    GuiFocusClearClient();
    GuiFocusHome();

    End = gSbCount - gViewOff;
    if (End < 0) {
        End = 0;
    }
    if (End > gSbCount) {
        End = gSbCount;
    }
    Start = End - Vis;
    if (gViewOff == 0 && gAccLen > 0) {
        /* 末行留给当前未完成行 */
        Start = End - (Vis - 1);
    }
    if (Start < 0) {
        Start = 0;
    }

    for (i = Start; i < End; i++) {
        const char *L = ConsoleSbLine(i);
        /* 历史行里的提示符也保持青色 */
        if (L[0] == 't' && L[1] == 'o' && L[2] == 'y' && L[3] == 'o' &&
            L[4] == 's' && L[5] == '>' && L[6] == ' ') {
            ConsoleDrawString("toyos> ", COLOR_CYAN);
            if (L[7]) {
                ConsoleDrawString(L + 7, COLOR_WHITE);
            }
        } else {
            ConsoleDrawString(L, COLOR_WHITE);
        }
        ConsoleDrawString("\n", COLOR_WHITE);
    }
    if (gViewOff == 0 && gAccLen > 0) {
        if (gAcc[0] == 't' && gAcc[1] == 'o' && gAcc[2] == 'y' && gAcc[3] == 'o' &&
            gAcc[4] == 's' && gAcc[5] == '>' && gAcc[6] == ' ') {
            ConsoleDrawString("toyos> ", COLOR_CYAN);
            if (gAcc[7]) {
                ConsoleDrawString(gAcc + 7, COLOR_WHITE);
            }
        } else {
            ConsoleDrawString(gAcc, COLOR_WHITE);
        }
    }

    GuiFocusSave();
    HalVideoClearClip();
    GuiBackupFocusWindow();
}

/* 若正在看历史，先回到底部再继续输出/输入 */
void ConsoleSbEnsureLive(void) {
    if (gViewOff == 0) {
        return;
    }
    gViewOff = 0;
    ConsoleSbPaint();
}

void ConsoleOnWheel(INT8 Wheel) {
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;
    UINT32 LineH;
    int Vis;
    int MaxOff;
    int Next;

    if (Wheel == 0 || !GuiShellAcceptsInput()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) || Cw == 0 || Ch == 0) {
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
    /* 正滚轮 = 看更早的行 → 增大 gViewOff */
    MaxOff = gSbCount - Vis;
    if (gAccLen > 0 && MaxOff > 0) {
        /* 留一行给当前输入时，历史上限略紧 */
        MaxOff = gSbCount - (Vis - 1);
    }
    if (MaxOff < 0) {
        MaxOff = 0;
    }
    Next = gViewOff + (int)Wheel;
    if (Next < 0) {
        Next = 0;
    }
    if (Next > MaxOff) {
        Next = MaxOff;
    }
    if (Next == gViewOff) {
        return;
    }
    gViewOff = Next;
    ConsoleSbPaint();
}
