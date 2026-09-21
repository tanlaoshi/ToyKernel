/*
 * GuiWm.c — 窗口全局态 / Init / 标题刷新（PR-S-guiwm-split-2）
 *
 * 开窗见 GuiOpen.c；点击与鼠标见 GuiPointer.c；命中见 GuiHit.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "Locale.h"
#include "CoreOps.h"

GUI_WINDOW gWindows[MAX_WINS];
UINT32 gScreenWidth;
UINT32 gScreenHeight;
UINT32 gCursorX;
UINT32 gCursorY;
UINT8  gCursorBtn;
int    gFocusWin;
int    gHoverWin = -1;

UINT32 gSaveX;
UINT32 gSaveY;
UINT32 gSaveWidth;
UINT32 gSaveHeight;
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
volatile int gComposeBusy;
int    gDeferPresent;
int    gShellEchoCoalesce; /* PR-G-shell-present：打字回显合并 Present */

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
        } else if (gWindows[i].Kind == GUI_WIN_STORE) {
            gWindows[i].Title = LocStr(MSG_APP_STORE);
        } else if (gWindows[i].Kind == GUI_WIN_DEVICES) {
            gWindows[i].Title = LocStr(MSG_APP_DEVICES);
        } else if (gWindows[i].Kind == GUI_WIN_FILES) {
            gWindows[i].Title = LocStr(MSG_APP_FILES);
        } else if (gWindows[i].Kind == GUI_WIN_EDIT) {
            gWindows[i].Title = "Edit";
        }
    }
    SyncWindowVisualsEx(1);
    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        StoreUiRepaint();
    } else if (GuiFocusKind() == GUI_WIN_DEVICES) {
        DevicesUiRepaint();
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
    gResizeWin = -1;

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
    DesktopSetClearIconFootprint(GuiClearIconDragFootprint);
    DesktopInit();
    {
        WINDOW_OPS Ops;

        Ops.OpenUser = GuiOpenUser;
        Ops.DamageUser = GuiDamageUser;
        Ops.DamageRectUser = GuiDamageRectUser;
        Ops.PollUserInput = GuiPollUserInput;
        Ops.AddButton = GuiUserAddButton;
        WindowOpsRegister(&Ops);
    }
    GuiRedraw();
    /* 桌面已铺满：停 GOP 叠字 boot log，避免「gui ready / ToyOS ready」留在壁纸上 */
    HalSerialGopMute(1);
    DebugWrite("Gui: desktop ready (icons + no app windows)\n");
}
