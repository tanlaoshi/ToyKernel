/*
 * GuiWm.c — PR-R2：窗口管理 / 开窗 / 鼠标路径编排
 */
#include "GuiPriv.h"
#include "UI.h"
#include "HalVideo.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Font.h"
#include "Debug.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Locale.h"
#include "PhysicalMemory.h"
#include "CoreOps.h"

GUI_WINDOW gWindows[MAX_WINS];
UINT32 gScreenWidth;
UINT32 gScreenHeight;
UINT32 gCursorX;
UINT32 gCursorY;
UINT8  gCursorBtn;
int    gFocusWin;

UINT32 gSaveX;
UINT32 gSaveY;
UINT32 gSaveW;
UINT32 gSaveH;
UINT32 gUnder[CURSOR_BOX * CURSOR_BOX];
int    gCursorVisible;

int    gDragWin = -1;
INT32  gDragOffX;
INT32  gDragOffY;
int    gDragArmed;
GUI_WINDOW gWinSwap;

int      gDragHasBackup;
int      gWinBackupValid[MAX_WINS];
UINT32   gWinBackupW[MAX_WINS];
UINT32   gWinBackupH[MAX_WINS];
UINT32   gWinBackupPages[MAX_WINS];
UINT32  *gWinBackup[MAX_WINS];
UINT32   gDragRowBuf[DRAG_ROW_MAX];
UINT32  *gDragDirty;
UINT32   gDragDirtyPages;
UINT32   gDragDirtyCap;

UINT32  *gScreenSnap;
UINT32   gScreenSnapPages;
int      gScreenSnapValid;
UINT32  *gUnderDrag;
UINT32   gUnderDragPages;
int      gUnderDragValid;
UINT32   gDragStartX;
UINT32   gDragStartY;
UINT32   gDragStartW;
UINT32   gDragStartH;

int    gGfxLockDepth;
UINT64 gGfxIrqFlags;
int    gComposeBusy;
int    gDeferPresent;
static int gInputLocked;
static UINT8 gMousePrevBtn;

GUI_CONSOLE_OPS gGuiConsoleOps;

void GuiRegisterConsoleOps(const GUI_CONSOLE_OPS *Ops) {
    if (!Ops) {
        gGuiConsoleOps.FocusSave = 0;
        gGuiConsoleOps.FocusLoad = 0;
        gGuiConsoleOps.OnShellOpened = 0;
        gGuiConsoleOps.PaintShellWindow = 0;
        return;
    }
    gGuiConsoleOps = *Ops;
}


void WinCopy(GUI_WINDOW *Dst, const GUI_WINDOW *Src) {
    *Dst = *Src;
}


int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (!W->Active) {
        return 0;
    }
    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    return X >= Bx && X < Bx + Bw && Y >= By && Y < By + Bh;
}


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


int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + W->Height;
}


int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + TITLE_HEIGHT;
}


int PointInAnyActiveWindow(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && PointInWindow(&gWindows[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}


int PointOnAnyClose(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}


/* 将窗口移到最前（数组后部 = 绘制在上层） */
void RaiseWindow(int Idx) {
    int Top = Idx;
    int J;
    int HasBackup = 0;

    for (J = Idx + 1; J < MAX_WINS; J++) {
        if (gWindows[J].Active) {
            Top = J;
        }
    }
    if (Top == Idx) {
        gFocusWin = Idx;
        return;
    }
    for (J = 0; J < MAX_WINS; J++) {
        if (gWinBackupValid[J] || gWinBackup[J] != 0) {
            HasBackup = 1;
            break;
        }
    }
    {
        WinCopy(&gWinSwap, &gWindows[Idx]);
        for (J = Idx; J < Top; J++) {
            WinCopy(&gWindows[J], &gWindows[J + 1]);
        }
        WinCopy(&gWindows[Top], &gWinSwap);
        /* 窗口与备份必须一起挪，否则会把别的窗备份贴到错误位置（花屏） */
        if (HasBackup || gDragHasBackup) {
            ShiftWinBackupsUp(Idx, Top);
        }
        gFocusWin = Top;
    }
}


/* 置顶并按备份重合成，避免只改焦点却在下层写穿 */
void GuiRaiseToFront(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    RaiseWindow(Idx);
    SyncWindowVisuals();
    GuiFocusApply();
    /* 顶层无有效备份时补内容，再抓一份干净备份 */
    if (gWindows[Idx].Kind == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_FILES) {
        FilesUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_EDIT) {
        EditUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_USER) {
        DrawWindowAtEx(Idx, 0);
        PaintUserClient(Idx);
        BackupWindowAtEx(Idx, 1);
    } else if (gWindows[Idx].Kind == GUI_WIN_SHELL && !gWinBackupValid[Idx]) {
        /* 欢迎语级恢复；完整历史需备份一直有效 */
        GuiConsoleOpsOnShellOpened();
        BackupWindowAt(Idx);
    }
}


int GuiPointInAnyWindow(UINT32 X, UINT32 Y) {
    return PointInAnyActiveWindow(X, Y);
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


GUI_WIN_KIND GuiWindowKind(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return GUI_WIN_NONE;
    }
    return gWindows[Idx].Kind;
}


GUI_WIN_KIND GuiFocusKind(void) {
    return GuiWindowKind(gFocusWin);
}


void GuiRefreshTitles(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        if (gWindows[i].Kind == GUI_WIN_SHELL) {
            gWindows[i].Title = LocStr(MSG_APP_SHELL);
        } else if (gWindows[i].Kind == GUI_WIN_SETTINGS) {
            gWindows[i].Title = LocStr(MSG_APP_SETTINGS);
        } else if (gWindows[i].Kind == GUI_WIN_FILES) {
            gWindows[i].Title = LocStr(MSG_APP_FILES);
        } else if (gWindows[i].Kind == GUI_WIN_EDIT) {
            gWindows[i].Title = "Edit";
        }
    }
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();
    SyncWindowVisualsEx(1);
    ComposeEnd();
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        EditUiRepaint();
    }
}


void GuiInit(void) {
    HalVideoGetSize(&gScreenWidth, &gScreenHeight);
    if (gScreenWidth == 0) {
        gScreenWidth = 1024;
        gScreenHeight = 768;
    }
    gCursorX = gScreenWidth / 2;
    gCursorY = gScreenHeight / 2;
    gFocusWin = -1;
    gCursorVisible = 0;
    gDragWin = -1;

    {
        int i;

        for (i = 0; i < MAX_WINS; i++) {
            gWindows[i].Active = 0;
            gWindows[i].Kind = GUI_WIN_NONE;
            gWindows[i].TermSet = 0;
            gWindows[i].InputLen = 0;
            gWindows[i].WaitPrompt = 0;
            gWindows[i].PromptShown = 0;
            gWindows[i].InputLine[0] = 0;
            gWindows[i].Title = "";
            gWindows[i].Background = ThemeShellClientBackground();
        }
    }

    PreallocWindowBackups();
    DesktopSetPointOccupied(GuiPointInAnyWindow);
    DesktopSetRequestRefresh(GuiRefreshDesktop);
    DesktopInit();
    {
        WINDOW_OPS Ops;

        Ops.OpenUser = GuiOpenUser;
        Ops.DamageUser = GuiDamageUser;
        Ops.PollUserInput = GuiPollUserInput;
        Ops.AddButton = GuiUserAddButton;
        WindowOpsRegister(&Ops);
    }
    GuiRedraw();
    /* 桌面已铺满：停 GOP 叠字 boot log，避免「gui ready / ToyOS ready」留在壁纸上 */
    HalSerialGopMute(1);
    DebugWrite("gui: desktop ready (icons + no app windows)\n");
}

void GuiOnDisplayResize(void) {
    int i;

    HalVideoGetSize(&gScreenWidth, &gScreenHeight);
    if (gScreenWidth == 0) {
        gScreenWidth = 1024;
    }
    if (gScreenHeight == 0) {
        gScreenHeight = 768;
    }
    if (gCursorX >= gScreenWidth) {
        gCursorX = gScreenWidth / 2;
    }
    if (gCursorY >= gScreenHeight) {
        gCursorY = gScreenHeight / 2;
    }
    gCursorVisible = 0;
    gDragWin = -1;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        gWinBackupValid[i] = 0;
        if (gWindows[i].Width > gScreenWidth) {
            gWindows[i].Width = gScreenWidth;
        }
        if (gWindows[i].Height > gScreenHeight) {
            gWindows[i].Height = gScreenHeight;
        }
        if (gWindows[i].X + gWindows[i].Width > gScreenWidth) {
            gWindows[i].X = (gScreenWidth > gWindows[i].Width)
                              ? (gScreenWidth - gWindows[i].Width)
                              : 0;
        }
        if (gWindows[i].Y + gWindows[i].Height > gScreenHeight) {
            gWindows[i].Y = (gScreenHeight > gWindows[i].Height)
                              ? (gScreenHeight - gWindows[i].Height)
                              : 0;
        }
    }

    DesktopOnDisplayResize();
    GuiRedraw();
    /* 热切持锁时勿重画 Settings：避免半成品 hit 与解锁后误点 */
    if (!GuiInputLocked()) {
        if (GuiFocusKind() == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
        } else if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiRepaint();
        } else if (GuiFocusKind() == GUI_WIN_EDIT) {
            EditUiRepaint();
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            GuiConsoleOpsPaintShellWindow(GuiFocusIndex());
        }
    }
    DebugWrite("gui: display resize ");
    DebugHex32(gScreenWidth);
    DebugWrite("x");
    DebugHex32(gScreenHeight);
    DebugWrite("\n");
}


void GuiOnArrowKey(UINT8 Key) {
    UINT32 X = gCursorX;
    UINT32 Y = gCursorY;
    UINT32 Step = 8;

    if (Key == 0x50 && X >= Step) {
        X -= Step;
    } else if (Key == 0x4F && X + Step < gScreenWidth) {
        X += Step;
    } else if (Key == 0x52 && Y >= Step) {
        Y -= Step;
    } else if (Key == 0x51 && Y + Step < gScreenHeight) {
        Y += Step;
    } else if (Key == 0x28) {
        GUI_MOUSE_STATE M;
        M.X = gCursorX;
        M.Y = gCursorY;
        M.Buttons = 1;
        M.Wheel = 0;
        GuiOnMouse(&M);
        M.Buttons = 0;
        GuiOnMouse(&M);
        return;
    } else {
        return;
    }
    CursorMove(X, Y);
}


/* PR-I3：右键占位 — 串口记一笔；不弹菜单（菜单另刀） */
void GuiRightClickPlaceholder(UINT32 X, UINT32 Y) {
    HalSerialWrite("gui: right-click\n");
    DebugWrite("gui: right-click x=");
    DebugHex32(X);
    DebugWrite(" y=");
    DebugHex32(Y);
    DebugWrite("\n");
}

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;
    int Hit;

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    /*
     * USER 按钮：在 Raise/Sync 之前命中顶层窗。
     * SyncWindowVisuals 会重贴备份；Raise 会搬槽，导致之后命中失败或事件写到错槽。
     */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWindows[i], X, Y)) {
            continue;
        }
        if (gWindows[i].Kind == GUI_WIN_USER && !PointInTitle(&gWindows[i], X, Y)) {
            Hit = UserButtonHit(i, X, Y);
            if (Hit >= 0) {
                gWindows[i].UserButtonClick = Hit;
                GuiFocusSave();
                RaiseWindow(i);
                /* 轻量置顶：勿 Sync 整桌（避免闪烁/吞事件） */
                GuiFocusApply();
                return 1;
            }
        }
        break; /* 顶层命中窗不是按钮，走下方通用逻辑 */
    }

    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (!PointInWindow(&gWindows[i], X, Y)) {
            continue;
        }
        GuiFocusSave();
        RaiseWindow(i);
        SyncWindowVisuals();
        GuiFocusApply();
        DebugWrite("gui: focus ");
        DebugWrite(gWindows[gFocusWin].Title);
        DebugWrite("\n");

        if (PointInTitle(&gWindows[gFocusWin], X, Y) &&
            !PointOnAnyClose(X, Y)) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
            RaiseWindow(gFocusWin);
            gDragWin = gFocusWin;
            gDragOffX = (INT32)X - (INT32)gWindows[gFocusWin].X;
            gDragOffY = (INT32)Y - (INT32)gWindows[gFocusWin].Y;
            gDragArmed = 1;
        }
        if (GuiFocusKind() == GUI_WIN_SETTINGS) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                SettingsUiRepaint();
            } else {
                SettingsUiOnClick(X, Y);
            }
        } else if (GuiFocusKind() == GUI_WIN_FILES) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                FilesUiRepaint();
            } else {
                FilesUiOnClick(X, Y);
            }
        } else if (GuiFocusKind() == GUI_WIN_EDIT) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                EditUiRepaint();
            } else {
                EditUiOnClick(X, Y);
            }
        } else if (GuiFocusKind() == GUI_WIN_SHELL &&
                   !gWinBackupValid[gFocusWin]) {
            GuiConsoleOpsOnShellOpened();
        } else if (gFocusWin >= 0) {
            BackupWindowAt(gFocusWin);
        }
        return 1;
    }
    /* 未点中窗口：桌面图标（双击打开） / 开始菜单 */
    {
        DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
        int Idx;

        if (!DesktopHandleClick(X, Y, &Act)) {
            return 0;
        }
        if (Act == DESKTOP_ACTION_SHELL) {
            Idx = GuiOpenShell();
            if (Idx >= 0) {
                GuiConsoleOpsOnShellOpened();
            }
        } else if (Act == DESKTOP_ACTION_SETTINGS) {
            (void)GuiOpenSettings();
        } else if (Act == DESKTOP_ACTION_FILES) {
            (void)GuiOpenFiles();
        }
        return 1;
    }
}


void GuiOnMouse(const GUI_MOUSE_STATE *Mouse) {
    /* 合成进行中只跟踪坐标/钮，避免嵌套 Move/Capture 采到半成品 FB。
     * 若光标仍画在旧位置，先擦掉，否则 Compose 期间移动会留下十字印。 */
    if (gComposeBusy) {
        if (gCursorVisible &&
            (Mouse->X != gCursorX || Mouse->Y != gCursorY)) {
            GfxIrqEnter();
            CursorRestore();
            HalVideoPresent();
            GfxIrqLeave();
        }
        gCursorX = Mouse->X;
        gCursorY = Mouse->Y;
        gCursorBtn = Mouse->Buttons;
        /* 仍推进边沿基准，避免合成结束后误触发按下 */
        gMousePrevBtn = Mouse->Buttons;
        return;
    }

    gCursorBtn = Mouse->Buttons;
    GuiPointerMove(Mouse->X, Mouse->Y);

    /* PR-I2：滚轮 — Files 列表 / Shell 客户区；其它忽略 */
    if (Mouse->Wheel != 0) {
        if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiOnWheel(Mouse->Wheel);
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleOnWheel(Mouse->Wheel);
        }
    }

    if ((Mouse->Buttons & 1) && !(gMousePrevBtn & 1)) {
        GuiHandleClick(gCursorX, gCursorY);
    } else if ((Mouse->Buttons & 1) && gDragWin >= 0) {
        /* PR-G10 L2：与 GuiPollMouse 统一，按住拖动时持续更新 */
        GuiDragUpdate(gCursorX, gCursorY);
    }
    if (!(Mouse->Buttons & 1) && (gMousePrevBtn & 1)) {
        GuiDragEnd();
    }
    /* PR-I3：右键按下边沿 → 占位回调（bit1） */
    if ((Mouse->Buttons & 2) && !(gMousePrevBtn & 2)) {
        GuiRightClickPlaceholder(gCursorX, gCursorY);
    }
    gMousePrevBtn = Mouse->Buttons;
}


void GuiInputLock(int Locked) {
    HAL_MOUSE_REPORT Raw;
    UINT8 LastBtn = gMousePrevBtn;

    if (Locked) {
        gInputLocked = 1;
        return;
    }
    /* 解锁前排空：热切期间堆积的边沿会在新分辨率下误点其它档 */
    if (HalMousePresent()) {
        HalInputPoll();
        while (HalMouseDequeue(&Raw)) {
            LastBtn = Raw.Buttons;
        }
    }
    gMousePrevBtn = LastBtn;
    gCursorBtn = LastBtn;
    gInputLocked = 0;
}

int GuiInputLocked(void) {
    return gInputLocked;
}

/* 从 XHCI 鼠标队列取报告并交给 GuiOnMouse（单一边沿/拖动逻辑） */
void GuiPollMouse(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    GUI_MOUSE_STATE M;
    UINT32 LastX;
    UINT32 LastY;
    UINT8 LastBtn;
    INT8 WheelSum;
    int Any;
    int NeedMove;

    if (!HalMousePresent()) {
        return;
    }

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gScreenWidth ? gScreenWidth : 1024;
    }
    if (Sh == 0) {
        Sh = gScreenHeight ? gScreenHeight : 768;
    }

    HalInputPoll();

    if (gInputLocked) {
        while (HalMouseDequeue(&Raw)) {
            gMousePrevBtn = Raw.Buttons;
            gCursorBtn = Raw.Buttons;
        }
        return;
    }

    /*
     * 真机：队列里常积几十份报告。逐条 CursorMove+Present → 光标极卡。
     * Defer Present，并合并位移；按键边沿/滚轮仍按每份报告处理。
     */
    LastX = gCursorX;
    LastY = gCursorY;
    LastBtn = gMousePrevBtn;
    WheelSum = 0;
    Any = 0;
    NeedMove = 0;
    GuiPresentDeferPush();
    while (HalMouseDequeue(&Raw)) {
        UINT32 X;
        UINT32 Y;

        if (Raw.Absolute || Raw.X > 4096u || Raw.Y > 4096u) {
            X = (UINT32)((UINT64)Raw.X * (UINT64)Sw / 32767ull);
            Y = (UINT32)((UINT64)Raw.Y * (UINT64)Sh / 32767ull);
        } else {
            X = Raw.X;
            Y = Raw.Y;
        }
        if (X >= Sw) {
            X = Sw > 0 ? Sw - 1 : 0;
        }
        if (Y >= Sh) {
            Y = Sh > 0 ? Sh - 1 : 0;
        }

        LastX = X;
        LastY = Y;
        NeedMove = 1;
        Any = 1;
        gCursorBtn = Raw.Buttons;
        if (Raw.Wheel != 0) {
            WheelSum = (INT8)(WheelSum + Raw.Wheel);
        }

        /* 按下/抬起边沿：必须逐包看；拖动位移合并到队尾再 Update */
        if ((Raw.Buttons & 1) && !(LastBtn & 1)) {
            GuiHandleClick(X, Y);
        }
        if (!(Raw.Buttons & 1) && (LastBtn & 1)) {
            GuiDragEnd();
        }
        if ((Raw.Buttons & 2) && !(LastBtn & 2)) {
            GuiRightClickPlaceholder(X, Y);
        }
        LastBtn = Raw.Buttons;
    }
    if (NeedMove) {
        if ((LastBtn & 1) && gDragWin >= 0) {
            gCursorX = LastX;
            gCursorY = LastY;
            GuiDragUpdate(LastX, LastY);
        } else {
            GuiPointerMove(LastX, LastY);
        }
    }
    if (WheelSum != 0) {
        M.X = LastX;
        M.Y = LastY;
        M.Buttons = LastBtn;
        M.Wheel = WheelSum;
        if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiOnWheel(M.Wheel);
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleOnWheel(M.Wheel);
        }
    }
    if (Any) {
        gMousePrevBtn = LastBtn;
        gCursorBtn = LastBtn;
    }
    GuiPresentDeferPop();
}

