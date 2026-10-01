/*
 * ConsoleHist.c — Shell ↑↓ 命令历史（PR-BOX-1）
 * 环形 ≥32；不持久化；无 Ctrl-R。
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Gui.h"
#include "Hal.h"
#include "HIDKeyboard.h"


#define HIST_MAX 32

static char gHist[HIST_MAX][LINE_MAX];
static int gHistCount;
static int gHistNext; /* 下次写入；满环时亦为最旧槽 */
static int gBrowse;   /* -1=当前行；0=最新；Count-1=最旧 */
static char gDraft[LINE_MAX];
static int gDraftLen;
static char gNavLine[LINE_MAX]; /* ↑↓ 选中快照 */
static int gNavActive;
static int gAnsi; /* 0 常态；1 见 ESC；2 见 ESC [ */
static int gReplacing;
static int gIgnoreRx; /* 串口回填 TX 后丢 pty echo，直到 Enter */

static int HistPhys(int DepthFromNewest) {
    if (gHistCount <= 0) {
        return 0;
    }
    if (gHistCount < HIST_MAX) {
        return gHistCount - 1 - DepthFromNewest;
    }
    return (gHistNext + HIST_MAX - 1 - DepthFromNewest) % HIST_MAX;
}

static int HistSameNewest(void) {
    int P;
    int i;

    if (gHistCount <= 0 || gLen <= 0) {
        return 0;
    }
    P = HistPhys(0);
    for (i = 0; i < gLen; i++) {
        if (gHist[P][i] != gLine[i]) {
            return 0;
        }
    }
    return gHist[P][gLen] == 0;
}

static void HistReplaceLine(const char *Text, int FromSerial) {
    int i;
    int NewLen;
    const char *Src;

    Src = (Text != 0) ? Text : "";
    gReplacing = 1;
    if (!FromSerial && GuiShellAcceptsInput()) {
        while (gLen > 0) {
            ConsoleOnBackspaceEx(0);
        }
        for (i = 0; Src[i] != 0; i++) {
            ConsoleOnCharEx(Src[i], 0);
        }
    } else {
        /* 串口：先 IgnoreRx 再 TX，挡 pty local-echo */
        gIgnoreRx = 1;
        NewLen = 0;
        while (Src[NewLen] != 0 && NewLen < LINE_MAX - 1) {
            NewLen++;
        }
        while (gLen > 0) {
            gLen--;
            HalConsoleBackspaceSerial();
        }
        for (i = 0; i < NewLen; i++) {
            gLine[i] = Src[i];
            HalConsolePutChar(Src[i]);
        }
        gLine[NewLen] = 0;
        gLen = NewLen;
    }
    NewLen = 0;
    while (Src[NewLen] != 0 && NewLen < LINE_MAX - 1) {
        gNavLine[NewLen] = Src[NewLen];
        NewLen++;
    }
    gNavLine[NewLen] = 0;
    gNavActive = 1;
    for (i = 0; i < NewLen; i++) {
        gLine[i] = gNavLine[i];
    }
    gLine[NewLen] = 0;
    gLen = NewLen;
    GuiConsolePush(gLine, gLen, gWaitPrompt);
    gReplacing = 0;
}

void ConsoleHistOnEdit(void) {
    if (gReplacing || gIgnoreRx) {
        return;
    }
    gBrowse = -1;
    gNavActive = 0;
    gAnsi = 0;
}

int ConsoleHistIsBusy(void) {
    return gReplacing;
}

int ConsoleHistIgnoreRx(void) {
    return gIgnoreRx;
}

void ConsoleHistClearIgnoreRx(void) {
    gIgnoreRx = 0;
}

void ConsoleHistApplyNavToLine(void) {
    int i;
    int P;

    if (gNavActive) {
        for (i = 0; gNavLine[i] != 0 && i < LINE_MAX - 1; i++) {
            gLine[i] = gNavLine[i];
        }
        gLine[i] = 0;
        gLen = i;
        gNavActive = 0;
        gBrowse = -1;
        return;
    }
    if (gBrowse >= 0 && gHistCount > 0) {
        P = HistPhys(gBrowse);
        for (i = 0; gHist[P][i] != 0 && i < LINE_MAX - 1; i++) {
            gLine[i] = gHist[P][i];
        }
        gLine[i] = 0;
        gLen = i;
        gBrowse = -1;
    }
}

void ConsoleHistPushLine(void) {
    int P;
    int i;

    /* 调用方须先 ConsoleHistApplyNavToLine */
    gAnsi = 0;
    gDraftLen = 0;
    if (gLen <= 0) {
        return;
    }
    if (HistSameNewest()) {
        return;
    }
    P = (gHistCount < HIST_MAX) ? gHistCount : gHistNext;
    for (i = 0; i < gLen && i < LINE_MAX - 1; i++) {
        gHist[P][i] = gLine[i];
    }
    gHist[P][i] = 0;
    if (gHistCount < HIST_MAX) {
        gHistCount++;
        gHistNext = gHistCount % HIST_MAX;
    } else {
        gHistNext = (gHistNext + 1) % HIST_MAX;
    }
}

void ConsoleOnHistArrow(int Down, int FromSerial) {
    int P;
    int i;

    if (ConsoleStdinUserHold()) {
        return;
    }
    if (!HalConsoleOnly() && !GuiShellAcceptsInput() && !FromSerial) {
        return;
    }
    if (gHistCount <= 0) {
        return;
    }

    if (!Down) {
        if (gBrowse < 0) {
            gDraftLen = gLen;
            for (i = 0; i < gLen && i < LINE_MAX - 1; i++) {
                gDraft[i] = gLine[i];
            }
            gDraft[i] = 0;
            gBrowse = 0;
        } else if (gBrowse < gHistCount - 1) {
            gBrowse++;
        } else {
            return;
        }
        P = HistPhys(gBrowse);
        HistReplaceLine(gHist[P], FromSerial);
        return;
    }

    if (gBrowse < 0) {
        return;
    }
    if (gBrowse == 0) {
        gDraft[gDraftLen < LINE_MAX ? gDraftLen : LINE_MAX - 1] = 0;
        HistReplaceLine(gDraft, FromSerial);
        gBrowse = -1;
        gNavActive = 0;
        return;
    }
    gBrowse--;
    P = HistPhys(gBrowse);
    HistReplaceLine(gHist[P], FromSerial);
}

int ConsoleHistFeedAnsi(char C, int FromSerial) {
    if (gAnsi == 0) {
        if (C == 0x1B) {
            gAnsi = 1;
            return 1;
        }
        return 0;
    }
    if (gAnsi == 1) {
        if (C == '[') {
            gAnsi = 2;
            return 1;
        }
        gAnsi = 0;
        return 0;
    }
    gAnsi = 0;
    if (C == 'A') {
        ConsoleOnHistArrow(0, FromSerial);
        return 1;
    }
    if (C == 'B') {
        ConsoleOnHistArrow(1, FromSerial);
        return 1;
    }
    return 1;
}

void ConsoleOnHistHid(UINT8 Key, int FromSerial) {
    if (Key == HID_KEY_UP) {
        ConsoleOnHistArrow(0, FromSerial);
    } else if (Key == HID_KEY_DOWN) {
        ConsoleOnHistArrow(1, FromSerial);
    }
}
