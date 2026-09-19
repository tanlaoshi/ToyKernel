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

/*
 * 开窗：Defer Present，先画满 chrome+客户区再备份，最后一次淡入。
 * 避免「空框 Present → 再填内容」闪白。
 */
static void OpenChromeDefer(int Idx) {
    GuiPresentDeferPush();
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
}

static void OpenFadeIn(int Idx) {
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gFocusWin);
    GuiAnimateWindowFade(gFocusWin, 1);
    GuiPresentDeferPop();
}

/*
 * 单例应用：Settings / Store（及 Edit）已有则前置焦点，不新开。
 * Shell / Files 允许多开（各占一槽；Files 内容态仍为全局宿主，见 FilesUi）。
 */
static int FocusExistingKind(GUI_WIN_KIND Kind, void (*Repaint)(void),
                             const char *LogTag) {
    int i;

    (void)LogTag;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && gWindows[i].Kind == Kind) {
            gFocusWin = i;
            RaiseWindow(i);
            SyncWindowVisuals();
            if (Repaint) {
                Repaint();
            }
            GuiFocusApply();
            BackupWindowAt(gFocusWin);
            DebugWrite("Gui: focus existing ");
            DebugWrite(LogTag);
            DebugWrite(" idx=");
            DebugHex32((UINT32)gFocusWin);
            DebugWrite("\n");
            return gFocusWin;
        }
    }
    return -1;
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

    /* PR-GUI-l3-fade：仍 Active 时淡出；随后走原收尾合成 */
    if (ThemeWindowFadeSteps() != 0) {
        if (!gWinBackupValid[Idx]) {
            BackupWindowAt(Idx);
        }
        GuiAnimateWindowFade(Idx, 0);
    }

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
    if (gResizeWin == Idx) {
        gResizeWin = -1;
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
    DebugWrite("Gui: closed window\n");
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
    /* 默认桌面 1280×720：960×600 约 ~85 列×~28 行（8×16），ps 等长行不易裁切 */
    if (W > 960) {
        W = 960;
    }
    if (H > 600) {
        H = 600;
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
    /* 先标已 prompt，避免 FocusApply→FocusLoad 抢画；下方 OnShellOpened 再画欢迎语 */
    gWindows[Idx].PromptShown = 1;
    gWindows[Idx].InputLine[0] = 0;

    OpenChromeDefer(Idx);
    GuiFocusSave();
    GuiConsoleOpsOnShellOpened();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open shell idx=");
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

    Idx = FocusExistingKind(GUI_WIN_SETTINGS, SettingsUiRepaint, "settings");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    /* 三分栏：与 Files 同量级，靠右 */
    W = 800;
    H = 560;
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

    OpenChromeDefer(Idx);
    SettingsUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open settings idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenStore(void) {
    int Idx;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 40;

    Idx = FocusExistingKind(GUI_WIN_STORE, StoreUiRepaint, "store");
    if (Idx >= 0) {
        return Idx;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 920;
    H = 600;
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

    OpenChromeDefer(Idx);
    StoreUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open store idx=");
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
    W = 800;
    H = 560;
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

    OpenChromeDefer(Idx);
    FilesUiOpen();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open files idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}


int GuiOpenEdit(const char *Path) {
    int Idx;
    int Existing;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Margin = 56;

    if (!Path || !Path[0]) {
        return -1;
    }

    /* Edit 亦单例：复用已有窗并换文件 */
    Existing = FocusExistingKind(GUI_WIN_EDIT, 0, "edit");
    if (Existing >= 0) {
        EditUiOpen(Path);
        DrawWindowAt(Existing);
        EditUiRepaint();
        BackupWindowAt(Existing);
        GuiFocusApply();
        BackupWindowAt(gFocusWin);
        return gFocusWin;
    }

    Idx = AllocWindowSlot();
    if (Idx < 0) {
        return -1;
    }
    W = 800;
    H = 520;
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

    OpenChromeDefer(Idx);
    EditUiOpen(Path);
    EditUiRepaint();
    OpenFadeIn(Idx);
    DebugWrite("Gui: open edit idx=");
    DebugHex32((UINT32)gFocusWin);
    DebugWrite("\n");
    return gFocusWin;
}
