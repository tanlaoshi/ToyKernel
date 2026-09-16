/*
 * GuiOpen.c — 关窗 / 开窗 / Place（PR-S-guiwm-split-2）
 *
 * 从 GuiWm.c 迁出；只搬家、不改逻辑。
 */
#include "GuiPriv.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Font.h"
#include "Debug.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Locale.h"

void CloseWindow(int Idx) {
    UINT32 X;
    UINT32 Y;
    UINT32 Ww;
    UINT32 Wh;
    int i;
    int SavedFocus;

    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    X = gWindows[Idx].X;
    Y = gWindows[Idx].Y;
    Ww = gWindows[Idx].Width;
    Wh = gWindows[Idx].Height;
    if (gWindows[Idx].Kind == GUI_WIN_USER) {
        gWindows[Idx].ClosePending = 1;
    }
    gWindows[Idx].Active = 0;
    gWindows[Idx].Kind = GUI_WIN_NONE;
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    gWindows[Idx].PromptShown = 0;
    gWindows[Idx].InputLine[0] = 0;
    gWindows[Idx].UserButtonClick = -1;
    gWindows[Idx].UserClientClick = 0;
    {
        int Bi;
        for (Bi = 0; Bi < 4; Bi++) {
            gWindows[Idx].UserButtonUsed[Bi] = 0;
            gWindows[Idx].UserButtonLabel[Bi][0] = 0;
        }
    }
    gWinBackupValid[Idx] = 0;
    if (gDragWin == Idx) {
        gDragWin = -1;
    }
    if (gHoverWin == Idx) {
        gHoverWin = -1;
    }
    if (gFocusWin == Idx) {
        gFocusWin = -1;
        for (i = MAX_WINS - 1; i >= 0; i--) {
            if (gWindows[i].Active) {
                gFocusWin = i;
                break;
            }
        }
    }

    /*
     * 关窗后整桌重合成：只擦关窗矩形会留下桌面/图标残影；
     * 相交窗备份常含被关窗像素（标题栏关闭钮被盖住时尤甚）→ 不透明重画+内容。
     */
    SavedFocus = gFocusWin;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    SyncWindowVisualsEx(1);
    gFocusWin = SavedFocus;
    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (!RectIntersects(gWindows[i].X, gWindows[i].Y, gWindows[i].Width, gWindows[i].Height,
                            X, Y, Ww, Wh)) {
            continue;
        }
        DrawWindowAtEx(i, 0);
        if (gWindows[i].Kind == GUI_WIN_SHELL) {
            GuiConsoleOpsPaintShellWindow(i);
        } else if (gWindows[i].Kind == GUI_WIN_SETTINGS) {
            gFocusWin = i;
            SettingsUiRepaint();
            gFocusWin = SavedFocus;
        } else if (gWindows[i].Kind == GUI_WIN_STORE) {
            gFocusWin = i;
            StoreUiRepaint();
            gFocusWin = SavedFocus;
        } else if (gWindows[i].Kind == GUI_WIN_FILES) {
            gFocusWin = i;
            FilesUiRepaint();
            gFocusWin = SavedFocus;
        } else if (gWindows[i].Kind == GUI_WIN_EDIT) {
            gFocusWin = i;
            EditUiRepaint();
            gFocusWin = SavedFocus;
        } else if (gWindows[i].Kind == GUI_WIN_USER) {
            PaintUserClient(i);
        }
        BackupWindowAtEx(i, 1);
    }
    gFocusWin = SavedFocus;
    ComposeEnd();
    GfxIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
    GuiFocusApply();
    DebugWrite("gui: closed window\n");
}


int AllocWindowSlot(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            return i;
        }
    }
    return -1;
}


void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY,
                           UINT32 *OutW, UINT32 *OutH) {
    UINT32 Margin = 48;
    UINT32 Cascade = (UINT32)Idx * 28;
    UINT32 W;
    UINT32 H;

    W = gScreenWidth > Margin * 2 + 200 ? gScreenWidth - Margin * 2 : gScreenWidth - 32;
    H = gScreenHeight > Margin * 2 + 120 ? gScreenHeight - Margin * 2 : gScreenHeight - 32;
    if (W > 720) {
        W = 720;
    }
    if (H > 480) {
        H = 480;
    }
    *OutX = Margin + Cascade;
    *OutY = Margin + Cascade;
    if (*OutX + W > gScreenWidth) {
        *OutX = Margin;
    }
    if (*OutY + H > gScreenHeight) {
        *OutY = Margin;
    }
    *OutW = W;
    *OutH = H;
}


int GuiOpenShell(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    PlaceNewWindow(Idx, &X, &Y, &W, &H);
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_SHELL;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeShellClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_SHELL);
    gWindows[Idx].TermSet = 0;
    gWindows[Idx].InputLen = 0;
    gWindows[Idx].WaitPrompt = 0;
    /* 先标已 prompt，避免 FocusApply→FocusLoad 抢画；OnShellOpened 再清客户区重画 */
    gWindows[Idx].PromptShown = 1;
    gWindows[Idx].InputLine[0] = 0;

    /* M3/G7：备份前必擦光标，避免十字烙进窗备份 */
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    HalVideoClearClip();
    DrawWindowAt(Idx);
    BackupWindowAt(Idx);
    ComposeEnd();
    GuiFocusSave();
    RaiseWindow(Idx);
    SyncWindowVisuals();
    GuiFocusApply();
    DebugWrite("gui: open shell idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenSettings(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 48;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    /* 靠右放置；高度按字体行距预留，避免菜单画出窗外叠在 Shell/桌面上 */
    W = 560;
    {
        UINT32 LineH = FontAdvanceY();
        UINT32 NeedH;

        if (LineH < 16) {
            LineH = 16;
        }
        /* 标题 + 边距 + Display 页约 14 行（含 Now/提示） */
        NeedH = TITLE_HEIGHT + GUI_CLIENT_PAD * 2 + 12 + LineH * 14 + 8;
        H = NeedH;
        if (H < 420) {
            H = 420;
        }
    }
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = (gScreenWidth > W + Margin) ? (gScreenWidth - W - Margin) : Margin;
    Y = Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_SETTINGS;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_SETTINGS);
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
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    SettingsUiOpen();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open settings idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenStore(void) {
    int Idx;
    int i;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    /* 单实例：已有 Store 则前置焦点，不新开 */
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_STORE) {
            gFocusWin = i;
            RaiseWindow(i);
            SyncWindowVisuals();
            StoreUiRepaint();
            GuiFocusApply();
            BackupWindowAt(gFocusWin);
            DebugWrite("gui: focus existing store idx=");
            DebugHex32((UINT32)gFocusWin);
            DebugWrite("\n");
            return gFocusWin;
        }
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 520;
    H = 420;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin;
    Y = Margin + 24;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_STORE;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_STORE);
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
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    StoreUiOpen();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open store idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenFiles(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 640;
    H = 480;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin;
    Y = (gScreenHeight > H + Margin) ? (gScreenHeight - H - Margin) : Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_FILES;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = LocStr(MSG_APP_FILES);
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
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    FilesUiOpen();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open files idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenEdit(const char *Path) {
    int Idx;
    int i;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 56;

    if (!Path || !Path[0]) {
        return -1;
    }

    /* 复用已有 Edit 窗 */
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_EDIT) {
            gFocusWin = i;
            RaiseWindow(i);
            SyncWindowVisuals();
            EditUiOpen(Path);
            DrawWindowAt(i);
            EditUiRepaint();
            BackupWindowAt(i);
            GuiFocusApply();
            BackupWindowAt(gFocusWin);
            return gFocusWin;
        }
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 640;
    H = 440;
    if (W + Margin * 2 > gScreenWidth) {
        W = gScreenWidth > Margin * 2 ? gScreenWidth - Margin * 2 : gScreenWidth / 2;
    }
    if (H + Margin * 2 > gScreenHeight) {
        H = gScreenHeight > Margin * 2 ? gScreenHeight - Margin * 2 : gScreenHeight / 2;
    }
    X = Margin + 24;
    Y = Margin;
    gWindows[Idx].Active = 1;
    gWindows[Idx].Kind = GUI_WIN_EDIT;
    gWindows[Idx].X = X;
    gWindows[Idx].Y = Y;
    gWindows[Idx].Width = W;
    gWindows[Idx].Height = H;
    gWindows[Idx].Background = ThemeSettingsClientBackground();
    gWindows[Idx].Title = "Edit";
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
    DrawWindowAt(Idx);
    ComposeEnd();
    gFocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
    EditUiOpen(Path);
    EditUiRepaint();
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    DebugWrite("gui: open edit idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}
