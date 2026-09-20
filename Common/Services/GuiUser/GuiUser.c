/*
 * GuiUser.c — 用户态窗口协议（核心）
 * 辅助：GuiUserBlit.c
 *
 * 从 GuiUser.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Font.h"

void CopyTitleBuf(char *Dst, UINTN Cap, const char *Src) {
    UINTN i;

    if (!Dst || Cap == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Cap && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

/* Raise 后槽位可能移动；返回当前 USER 窗下标，失败 -1 */
int UserWindowIndexAfterRaise(int Wid) {
    int i;

    if (Wid >= 0 && Wid < MAX_WINS && gWindows[Wid].Active &&
        gWindows[Wid].Kind == GUI_WIN_USER) {
        RaiseWindow(Wid);
    } else {
        for (i = 0; i < MAX_WINS; i++) {
            if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_USER) {
                RaiseWindow(i);
                break;
            }
        }
    }
    if (gFocusWin >= 0 && gFocusWin < MAX_WINS &&
        gWindows[gFocusWin].Active && gWindows[gFocusWin].Kind == GUI_WIN_USER) {
        return gFocusWin;
    }
    return -1;
}


/* 不透明重画用户窗内容并 ForceFull 备份，避免下层透视烙进备份 */
void RepaintUserWindow(int Wid) {
    int Idx;

    /* 开窗/加按钮期间禁鼠标边沿，避免误点进 Poll */
    GuiInputLock(1);
    Idx = UserWindowIndexAfterRaise(Wid);
    if (Idx < 0) {
        GuiInputLock(0);
        return;
    }
    /* 丢弃可能含透视的旧备份，Sync 时走 DrawWindowAt */
    gWinBackupValid[Idx] = 0;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    SyncWindowVisualsEx(0);
    /* Sync 后仍强制不透明整窗，再画控件 */
    DrawWindowAtEx(Idx, 0);
    PaintUserClient(Idx);
    BackupWindowAtEx(Idx, 1);
    GuiFocusApply();
    ComposeEnd();
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    gWindows[Idx].UserButtonClick = -1;
    gWindows[Idx].UserClientClick = 0;
    gWindows[Idx].UserKeyCount = 0;
    GuiInputLock(0);
}


int UserButtonHit(int Idx, UINT32 X, UINT32 Y) {
    GUI_WINDOW *W;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Gap = 8;
    int Bi;
    int Count;
    int Slot;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return -1;
    }
    W = &gWindows[Idx];
    if (!W->Active || W->Kind != GUI_WIN_USER) {
        return -1;
    }
    Cx = W->X + 1 + GUI_CLIENT_PAD;
    Cy = W->Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
    Cw = W->Width - 2 - GUI_CLIENT_PAD * 2;
    Ch = W->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
    if (Ch < 48) {
        return -1;
    }
    Count = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        if (W->UserButtonUsed[Bi]) {
            Count++;
        }
    }
    if (Count == 0) {
        return -1;
    }
    Bh = 36;
    Bw = (Cw - Gap * (Count + 1)) / (UINT32)Count;
    if (Bw < 64) {
        Bw = 64;
    }
    if (Bw > 160) {
        Bw = 160;
    }
    By = Cy + Ch - Bh - 4;
    Bx = Cx + Gap;
    Slot = 0;
    for (Bi = 0; Bi < 4; Bi++) {
        UINT32 Left;
        UINT32 Right;

        if (!W->UserButtonUsed[Bi]) {
            continue;
        }
        Left = Bx + Slot * (Bw + Gap);
        Right = Left + Bw;
        if (X >= Left && X < Right && Y >= By && Y < By + Bh) {
            return Bi;
        }
        Slot++;
    }
    return -1;
}


int GuiOpenUser(const char *Title, UINT32 W, UINT32 H) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 Margin = 48;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    if (W < 160) {
        W = 160;
    }
    if (H < 100) {
        H = 100;
    }
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = (gScreenWidth > W) ? (gScreenWidth - W) / 2 : 0;
    Y = (gScreenHeight > H + 40) ? (gScreenHeight - H) / 3 : Margin;

    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_USER;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    CopyTitleBuf(gWindows[Idx].TitleBuf, sizeof(gWindows[Idx].TitleBuf), Title);
    gWindows[Idx].Title = gWindows[Idx].TitleBuf;
    gWindows[Idx].ClientText[0] = 0;
    gWindows[Idx].ClosePending = 0;
    gWindows[Idx].Closing = 0;
    gWindows[Idx].UserButtonClick = -1;
    gWindows[Idx].UserClientClick = 0;
    gWindows[Idx].UserClickX = 0;
    gWindows[Idx].UserClickY = 0;
    gWindows[Idx].UserKeyCount = 0;
    {
        int Bi;
        for (Bi = 0; Bi < 4; Bi++) {
            gWindows[Idx].UserButtonUsed[Bi] = 0;
            gWindows[Idx].UserButtonLabel[Bi][0] = 0;
        }
    }
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;

    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    DrawWindowAtEx(Idx, 0);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    Idx = gFocusWin;
    gWinBackupValid[Idx] = 0;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    SyncWindowVisualsEx(0);
    DrawWindowAtEx(Idx, 0);
    PaintUserClient(Idx);
    BackupWindowAtEx(Idx, 1);
    GuiFocusApply();
    ComposeEnd();
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    DebugWrite("Gui: open user idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}

int GuiUserAddButton(int Wid, int ButtonId, const char *Label) {
    if (Wid < 0 || Wid >= MAX_WINS || !gWindows[Wid].Active ||
        gWindows[Wid].Kind != GUI_WIN_USER) {
        return -1;
    }
    if (ButtonId < 0 || ButtonId >= 4 || !Label) {
        return -1;
    }
    gWindows[Wid].UserButtonUsed[ButtonId] = 1;
    CopyTitleBuf(gWindows[Wid].UserButtonLabel[ButtonId],
                 sizeof(gWindows[Wid].UserButtonLabel[ButtonId]), Label);
    RepaintUserWindow(Wid);
    return 0;
}
