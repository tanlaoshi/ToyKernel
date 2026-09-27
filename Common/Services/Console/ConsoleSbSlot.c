/*
 * ConsoleSbSlot.c — 每 Shell 窗独立 scrollback
 *
 * 焦点切换 Save/Load；RaiseWindow 挪槽时 ShiftRaise 跟 GUI_WINDOW 一起挪。
 * 注意：TASK 栈仅 8KB，CONSOLE_SB_STATE≈7.8KB，禁止作局部变量（会死机）。
 */
#include "ConsolePrivate.h"
#include "Gui.h"
#include "Hal.h"

static CONSOLE_SB_STATE gSlot[GUI_MAX_WINS];
static int gSlotUsed[GUI_MAX_WINS];
static int gLiveWin = -1;
/* Raise 挪槽用：勿放栈上 */
static CONSOLE_SB_STATE gRaiseSwap;

static void SlotCopy(CONSOLE_SB_STATE *Dst, const CONSOLE_SB_STATE *Src) {
    UINTN i;
    const UINT8 *S = (const UINT8 *)Src;
    UINT8 *D = (UINT8 *)Dst;

    for (i = 0; i < sizeof(CONSOLE_SB_STATE); i++) {
        D[i] = S[i];
    }
}

static void SyncLiveIntoSlot(int Idx) {
    if (Idx < 0 || Idx >= GUI_MAX_WINS) {
        return;
    }
    ConsoleSbCapture(&gSlot[Idx]);
    gSlotUsed[Idx] = 1;
}

void ConsoleSbBindFocus(void) {
    int Idx;

    if (HalConsoleOnly()) {
        return;
    }
    Idx = GuiFocusIndex();
    if (!GuiShellWindowActive(Idx)) {
        return;
    }
    if (gLiveWin != Idx) {
        ConsoleSbWinLoad(Idx);
    }
}

void ConsoleSbFeedLiveIfBound(const char *Text) {
    if (!Text || gLiveWin < 0 || gLiveWin >= GUI_MAX_WINS) {
        return;
    }
    ConsoleSbFeed(Text);
    /* 不在此 Sync 整槽（7.8KB/次）；切焦点时 Save 即可 */
}

void ConsoleSbWinSave(int Idx) {
    if (Idx < 0 || Idx >= GUI_MAX_WINS) {
        return;
    }
    /* 曾写 live 却未 Bind：认领到本窗，避免开第二窗时历史丢失 */
    if (gLiveWin < 0) {
        gLiveWin = Idx;
    }
    if (gLiveWin != Idx) {
        return;
    }
    SyncLiveIntoSlot(Idx);
}

void ConsoleSbWinLoad(int Idx) {
    if (Idx < 0 || Idx >= GUI_MAX_WINS) {
        if (gLiveWin >= 0) {
            SyncLiveIntoSlot(gLiveWin);
        }
        ConsoleSbReset();
        gLiveWin = -1;
        return;
    }
    if (gLiveWin == Idx) {
        return;
    }
    if (gLiveWin >= 0 && gLiveWin < GUI_MAX_WINS) {
        SyncLiveIntoSlot(gLiveWin);
    }
    gLiveWin = Idx;
    if (gSlotUsed[Idx]) {
        ConsoleSbApply(&gSlot[Idx]);
    } else {
        ConsoleSbReset();
    }
}

void ConsoleSbWinForget(int Idx) {
    if (Idx < 0 || Idx >= GUI_MAX_WINS) {
        return;
    }
    gSlotUsed[Idx] = 0;
    if (gLiveWin == Idx) {
        ConsoleSbReset();
        gLiveWin = -1;
    }
}

int ConsoleSbWinHasContent(int Idx) {
    if (Idx < 0 || Idx >= GUI_MAX_WINS) {
        return 0;
    }
    if (gLiveWin == Idx) {
        return ConsoleSbHasContent();
    }
    if (!gSlotUsed[Idx]) {
        return 0;
    }
    return (gSlot[Idx].Count > 0 || gSlot[Idx].AccLen > 0) ? 1 : 0;
}

void ConsoleSbWinFeed(int Idx, const char *Text) {
    int SavedLive;

    if (!Text || Idx < 0 || Idx >= GUI_MAX_WINS) {
        return;
    }
    if (gLiveWin == Idx) {
        ConsoleSbFeed(Text);
        return;
    }
    SavedLive = gLiveWin;
    if (SavedLive >= 0) {
        SyncLiveIntoSlot(SavedLive);
    }
    if (gSlotUsed[Idx]) {
        ConsoleSbApply(&gSlot[Idx]);
    } else {
        ConsoleSbReset();
    }
    gLiveWin = Idx;
    ConsoleSbFeed(Text);
    SyncLiveIntoSlot(Idx);
    if (SavedLive >= 0 && SavedLive < GUI_MAX_WINS) {
        ConsoleSbApply(&gSlot[SavedLive]);
        gLiveWin = SavedLive;
    } else {
        ConsoleSbReset();
        gLiveWin = -1;
    }
}

/*
 * 与 RaiseWindow 相同：把 Idx 捅到 Top，中间槽前移。
 * 须在 WinCopy 之后调用，并修正 gLiveWin。
 */
void ConsoleSbShiftRaise(int Idx, int Top) {
    int TmpUsed;
    int J;

    if (Idx < 0 || Top < 0 || Idx >= GUI_MAX_WINS || Top >= GUI_MAX_WINS) {
        return;
    }
    if (Top == Idx) {
        return;
    }
    if (gLiveWin >= 0 && gLiveWin < GUI_MAX_WINS) {
        SyncLiveIntoSlot(gLiveWin);
    }
    SlotCopy(&gRaiseSwap, &gSlot[Idx]);
    TmpUsed = gSlotUsed[Idx];
    for (J = Idx; J < Top; J++) {
        SlotCopy(&gSlot[J], &gSlot[J + 1]);
        gSlotUsed[J] = gSlotUsed[J + 1];
    }
    SlotCopy(&gSlot[Top], &gRaiseSwap);
    gSlotUsed[Top] = TmpUsed;
    if (gLiveWin == Idx) {
        gLiveWin = Top;
    } else if (gLiveWin > Idx && gLiveWin <= Top) {
        gLiveWin--;
    }
}
