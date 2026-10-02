/*
 * ConsoleScroll.c — Shell 行缓冲 / 历史 / 滚轮（PR-S3-consolescroll-1）
 *
 * 重画见 ConsoleSbPaint.c。不改语义。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "Font.h"

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
    ConsoleSbBarReset();
}

void ConsoleSbCapture(CONSOLE_SB_STATE *Out) {
    UINTN i;
    const UINT8 *Live;
    UINT8 *D;

    if (!Out) {
        return;
    }
    Out->Count = gSbCount;
    Out->Next = gSbNext;
    Out->ViewOff = gViewOff;
    Out->AccLen = gAccLen;
    for (i = 0; i < SB_COLS; i++) {
        Out->Acc[i] = gAcc[i];
    }
    Live = (const UINT8 *)&gSb[0][0];
    D = (UINT8 *)&Out->Lines[0][0];
    for (i = 0; i < (UINTN)SB_LINES * (UINTN)SB_COLS; i++) {
        D[i] = Live[i];
    }
}

void ConsoleSbApply(const CONSOLE_SB_STATE *In) {
    UINTN i;
    const UINT8 *S;
    UINT8 *Live;

    ConsoleSbBarReset();
    if (!In) {
        ConsoleSbReset();
        return;
    }
    gSbCount = In->Count;
    gSbNext = In->Next;
    gViewOff = In->ViewOff;
    gAccLen = In->AccLen;
    for (i = 0; i < SB_COLS; i++) {
        gAcc[i] = In->Acc[i];
    }
    S = (const UINT8 *)&In->Lines[0][0];
    Live = (UINT8 *)&gSb[0][0];
    for (i = 0; i < (UINTN)SB_LINES * (UINTN)SB_COLS; i++) {
        Live[i] = S[i];
    }
}

int ConsoleSbLineCount(void) {
    return gSbCount;
}

int ConsoleSbAccLen(void) {
    return gAccLen;
}

const char *ConsoleSbAcc(void) {
    return gAcc;
}

int ConsoleSbViewOff(void) {
    return gViewOff;
}

int ConsoleSbMaxOff(int Vis) {
    int MaxOff;

    if (Vis < 1) {
        Vis = 1;
    }
    MaxOff = gSbCount - Vis;
    if (gAccLen > 0 && MaxOff > 0) {
        MaxOff = gSbCount - (Vis - 1);
    }
    if (MaxOff < 0) {
        MaxOff = 0;
    }
    return MaxOff;
}

void ConsoleSbSetViewOff(int Next, int MaxOff) {
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

const char *ConsoleSbLine(int OldestIndex) {
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

int ConsoleSbHasContent(void) {
    return (gSbCount > 0 || gAccLen > 0) ? 1 : 0;
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
    MaxOff = ConsoleSbMaxOff(Vis);
    ConsoleSbSetViewOff(gViewOff + (int)Wheel, MaxOff);
}
