/*
 * GuiCompose.c — Present / 主题场景编排（PR-S-compose-split-1）
 *
 * 窗绘制见 GuiDraw.c；备份见 GuiBackup.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Theme.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "Scheduler.h"

void GfxIrqEnter(void) {
    if (gGfxLockDepth++ == 0) {
        SchedulerPreemptDisable();
        gGfxIrqFlags = HalIrqSave();
    }
}


void GfxIrqLeave(void) {
    if (gGfxLockDepth > 0 && --gGfxLockDepth == 0) {
        HalIrqRestore(gGfxIrqFlags);
        SchedulerPreemptEnable();
    }
}


void ComposeBegin(void) {
    if (gComposeBusy++ == 0) {
        SchedulerPreemptDisable();
    }
}


void ComposeEnd(void) {
    if (gComposeBusy > 0 && --gComposeBusy == 0) {
        SchedulerPreemptEnable();
    }
}

/* 先擦光标再置 Busy，避免 SMP 上 Busy 抢在 Restore 前清 Visible → 镂空 */
void ComposeBeginEraseCursor(void) {
    GfxIrqEnter();
    CursorRestore();
    ComposeBegin();
    GfxIrqLeave();
}


/* 主题合成中推迟 Present；拖动等路径仍立即提交 */
void GfxPresent(void) {
    if (gDeferPresent) {
        gShellEchoCoalesce = 0;
        return;
    }
    /*
     * PR-G-shell-present：Shell 打字回显先写后缓冲，跳过本帧 Present。
     * ShellTask 每轮末尾 HalVideoPresent 会合并刷出；help/ls 仍走 Defer*。
     */
    if (gShellEchoCoalesce) {
        gShellEchoCoalesce = 0;
        return;
    }
    HalVideoPresent();
}

void GuiPresentShellEchoMark(void) {
    gShellEchoCoalesce = 1;
}

void GuiPresentDeferPush(void) {
    gDeferPresent++;
}

void GuiPresentDeferPop(void) {
    if (gDeferPresent > 0) {
        gDeferPresent--;
    }
    gShellEchoCoalesce = 0;
    if (gDeferPresent == 0) {
        HalVideoPresent();
    }
}


void GuiRedraw(void) {
    int i;

    /* G7：桌面/窗体开中断绘制；ComposeBusy 丢弃嵌套鼠标；只锁光标 */
    ComposeBeginEraseCursor();
    HalVideoClearClip();
    DesktopFillRect(0, 0, gScreenWidth, gScreenHeight);
    DesktopDraw();
    for (i = 0; i < MAX_WINS; i++) {
        DrawWindowAt(i);
    }
    DesktopDrawStartMenu();
    DesktopDrawNetTrayPopup();
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    HalVideoPresentFlush();
    ComposeEnd();
}


/* PR-G13：菜单开合后整屏合成（开/关都走 Compose，避免 Sync 备份残留方块烙印） */
void GuiRefreshDesktop(void) {
    if (DesktopStartMenuIsOpen()) {
        /* 弹出开始菜单：其它窗失焦；禁止半透/chrome 镂进菜单 */
        if (gFocusWin >= 0) {
            GuiFocusSave();
            gFocusWin = -1;
        }
        gHoverWin = -1;
    }
    GuiComposeThemeScene();
}


void GuiApplyThemeColors(void) {
    int i;
    UINT32 Bg = ThemeShellClientBackground();

    /* 只更新属性；整屏提交见 GuiComposeThemeScene（PR-G8） */
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_SHELL) {
            gWindows[i].Background = Bg;
            gWindows[i].TermSet = 0;
            gWindows[i].TermX = 0;
            gWindows[i].TermY = 0;
            gWindows[i].InputLen = 0;
            gWindows[i].InputLine[0] = 0;
            /* 勿清 PromptShown：否则 Compose 后切回 Shell 会在 toyos> 后再打欢迎语 */
            gWindows[i].WaitPrompt = 0;
        } else if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_SETTINGS) {
            gWindows[i].Background = ThemeSettingsClientBackground();
        } else if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_STORE) {
            gWindows[i].Background = ThemeSettingsClientBackground();
        } else if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_DEVICES) {
            gWindows[i].Background = ThemeSettingsClientBackground();
        } else if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_FILES) {
            gWindows[i].Background = ThemeSettingsClientBackground();
        } else if (gWindows[i].Active && gWindows[i].Kind == GUI_WIN_EDIT) {
            gWindows[i].Background = ThemeSettingsClientBackground();
        }
    }
}


/*
 * PR-G8/G9：主题一次合成（painter's algorithm，后缓冲上完成再 Present）：
 * 1) 整屏桌面 + 图标；2) 自下而上不透明整窗 + 内容；每窗立刻 ForceFull 备份；
 * gDeferPresent 避免中间态刷到 GOP（灰闪 / 下层盖上层）。
 */
void GuiComposeThemeScene(void) {
    int i;
    int SavedFocus = gFocusWin;

    gDeferPresent = 1;
    ComposeBeginEraseCursor();
    HalVideoClearClip();

    /* 先铺底：有 DeferPresent 时整屏 wipe 不会露到屏幕 */
    DesktopFillRect(0, 0, gScreenWidth, gScreenHeight);
    DesktopDraw();

    for (i = 0; i < MAX_WINS; i++) {
        if (!gWindows[i].Active) {
            continue;
        }
        /*
         * 标题栏颜色看 gFocusWin。画 Shell 内容时会暂把焦点设到该窗；
         * 若不先恢复 SavedFocus，后画的 Settings 标题会被画成灰色（失焦）。
         */
        gFocusWin = SavedFocus;
        DrawWindowAtEx(i, 0);
        if (gWindows[i].Kind == GUI_WIN_SHELL) {
            gFocusWin = i;
            GuiConsoleOpsPaintShellWindow(i);
        } else if (gWindows[i].Kind == GUI_WIN_SETTINGS) {
            gFocusWin = i;
            SettingsUiPaintFocused();
        } else if (gWindows[i].Kind == GUI_WIN_STORE) {
            gFocusWin = i;
            StoreUiPaintFocused();
        } else if (gWindows[i].Kind == GUI_WIN_DEVICES) {
            gFocusWin = i;
            DevicesUiPaintFocused();
        } else if (gWindows[i].Kind == GUI_WIN_FILES) {
            gFocusWin = i;
            FilesUiPaintFocused();
        } else if (gWindows[i].Kind == GUI_WIN_EDIT) {
            gFocusWin = i;
            EditUiPaintFocused();
        } else if (gWindows[i].Kind == GUI_WIN_USER) {
            gFocusWin = i;
            PaintUserClient(i);
        }
        /* 上层尚未画上：整窗备份，避免重叠区镂空透视 */
        BackupWindowAtEx(i, 1);
    }

    /* 全部 ForceFull 备份完成后再画影，避免下层备份吸入上层阴影 */
    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            DrawWindowShadowAt(i);
        }
    }

    /* 开始菜单 / 网络托盘盖住所有窗；须清 Shell 客户区 clip（FillRect/文字受 clip，BMP 图标走 WriteRect 不受） */
    HalVideoClearClip();
    DesktopDrawStartMenu();
    DesktopDrawNetTrayPopup();

    gFocusWin = SavedFocus;
    if (SavedFocus >= 0 && SavedFocus < MAX_WINS && gWindows[SavedFocus].Active) {
        GuiFocusApply();
    } else {
        HalVideoClearClip();
    }
    GfxIrqEnter();
    CursorPaint();
    GfxIrqLeave();
    gDeferPresent = 0;
    /*
     * PresentFlush 必须仍在 ComposeBusy 内：NUC 8 核上 AP 鼠标否则会
     * CursorRestore 旧 gUnder → 正方形镂空；任务栏半截 Present 同窗。
     */
    HalVideoPresentFlush();
    ComposeEnd();
}


void GuiPaintWindow(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    GfxIrqEnter();
    CursorRestore();
    DrawWindowAt(Idx);
    CursorPaint();
    HalVideoPresent();
    GfxIrqLeave();
}


void GuiBackupAllWindows(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active) {
            BackupWindowAt(i);
        }
    }
}

