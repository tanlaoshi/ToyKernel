/*
 * GuiPointer.c — 显示缩放钳窗 / 点击 / 鼠标编排（PR-S-guiwm-split-2）
 *
 * 从 GuiWm.c 迁出；只搬家、不改逻辑。
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Process.h"
#include "ToySerialLog.h"

static int gInputLocked;
static UINT8 gMousePrevBtn;

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
    /*
     * 相对 USB 鼠把像素累加在驱动 Abs 里；只改 gCursor* 不同步 →
     * 下一帧仍按旧大坐标入队，Gui 钳到 Sw-1 → 十字钉在右缘，要挪很久才进桌面。
     * 平板 Absolute 路径不读 Abs，排空队列仍无害。
     */
    HalInputMouseHandoffDesktop(gCursorX, gCursorY);
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
    /*
     * 必须全窗合成（含客户区）。勿仅 GuiRedraw 画 chrome：
     * Settings 改 scale 时常持 GuiInputLock，旧逻辑会跳过内容重绘 →
     * 其它窗用户区空白。Compose 不受 lock 影响（lock 只挡点击）。
     */
    GuiComposeThemeScene();
    DebugWrite("Gui: display resize ");
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


/* PR-I3：右键占位 — 仅串口记一笔；不弹菜单、不派发点击（菜单另刀） */
void GuiRightClickPlaceholder(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
    ToyLogGui("Gui: right-click\n");
}

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;
    int Hit;
    DESKTOP_ACTION Act = DESKTOP_ACTION_NONE;
    char ExecPath[96];

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    ExecPath[0] = 0;
    /*
     * 开始菜单 / 网络托盘聚焦优先级：
     * 1) 点在菜单/flyout/开始钮/托盘 → DesktopHandleClick
     * 2) 点在菜单外且落在窗上 → HandleTaskbarClick 已收起，再 fall through 聚焦置顶
     * 3) 菜单未开时任务栏仍优先于窗（开始钮）
     */
    {
        int DoDesktop = 0;

        if (DesktopStartMenuIsOpen() || DesktopNetTrayIsOpen()) {
            if (DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
                DoDesktop = 1;
            }
            /* 未命中：已收起；继续下面 Raise 窗 */
        } else if (DesktopClickOnTaskbar(X, Y) &&
                   DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            DoDesktop = 1;
        }
        if (DoDesktop) {
            if (DesktopIconDragActive()) {
                GfxIrqEnter();
                CursorRestore();
                GfxIrqLeave();
            }
            if (Act == DESKTOP_ACTION_SHELL) {
                (void)GuiOpenShell();
            } else if (Act == DESKTOP_ACTION_SETTINGS) {
                (void)GuiOpenSettings();
            } else if (Act == DESKTOP_ACTION_FILES) {
                (void)GuiOpenFiles();
            } else if (Act == DESKTOP_ACTION_STORE) {
                (void)GuiOpenStore();
            } else if (Act == DESKTOP_ACTION_EXEC) {
                if (ExecPath[0]) {
                    DebugWrite("desktop: exec ");
                    DebugWrite(ExecPath);
                    DebugWrite("\n");
                    (void)ProcessExec(ExecPath);
                }
            } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
                HalCpuShutdown();
            } else if (Act == DESKTOP_ACTION_REBOOT) {
                HalCpuReboot();
            }
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
            /* PR-A-ui-api：客户区点击（相对 ToyGfx 原点，含 pad） */
            {
                UINT32 Cx = gWindows[i].X + 1 + GUI_CLIENT_PAD;
                UINT32 Cy = gWindows[i].Y + TITLE_HEIGHT + GUI_CLIENT_PAD;
                if (X >= Cx && Y >= Cy) {
                    gWindows[i].UserClientClick = 1;
                    gWindows[i].UserClickX = X - Cx;
                    gWindows[i].UserClickY = Y - Cy;
                }
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
        DebugWrite("Gui: focus ");
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
                /* 客户区重绘后强制刷新标题，清光标/透视残块 */
                GuiFrameBufferBegin();
                DrawWindowChromeAt(gFocusWin);
                GuiFrameBufferEnd();
            }
            /* 客户区：按下高亮由 OnPointer；抬起才触发（见 SettingsUiOnPointer / StoreUiOnPointer） */
        } else if (GuiFocusKind() == GUI_WIN_STORE) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                StoreUiRepaint();
                GuiFrameBufferBegin();
                DrawWindowChromeAt(gFocusWin);
                GuiFrameBufferEnd();
            }
            /* 客户区：同 Settings，不在按下时 OnClick */
        } else if (GuiFocusKind() == GUI_WIN_FILES) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                FilesUiRepaint();
                GuiFrameBufferBegin();
                DrawWindowChromeAt(gFocusWin);
                GuiFrameBufferEnd();
            } else {
                FilesUiOnClick(X, Y);
            }
        } else if (GuiFocusKind() == GUI_WIN_EDIT) {
            if (PointInTitle(&gWindows[gFocusWin], X, Y)) {
                EditUiRepaint();
                GuiFrameBufferBegin();
                DrawWindowChromeAt(gFocusWin);
                GuiFrameBufferEnd();
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
        char ExecPath[96];

        ExecPath[0] = 0;
        if (!DesktopHandleClick(X, Y, &Act, ExecPath, sizeof(ExecPath))) {
            return 0;
        }
        /* PR-G-desk-1：与窗标题拖一致，武装拖放前先擦光标，避免留下光标脏块 */
        if (DesktopIconDragActive()) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
        }
        if (Act == DESKTOP_ACTION_SHELL) {
            (void)GuiOpenShell();
        } else if (Act == DESKTOP_ACTION_SETTINGS) {
            (void)GuiOpenSettings();
        } else if (Act == DESKTOP_ACTION_FILES) {
            (void)GuiOpenFiles();
        } else if (Act == DESKTOP_ACTION_STORE) {
            (void)GuiOpenStore();
        } else if (Act == DESKTOP_ACTION_EXEC) {
            /* PR-G-desk-2：与 Files 双击 ELF 同路径；阻塞至进程退出 */
            if (ExecPath[0]) {
                DebugWrite("desktop: exec ");
                DebugWrite(ExecPath);
                DebugWrite("\n");
                (void)ProcessExec(ExecPath);
            }
        } else if (Act == DESKTOP_ACTION_SHUTDOWN) {
            HalCpuShutdown();
        } else if (Act == DESKTOP_ACTION_REBOOT) {
            HalCpuReboot();
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

    if ((Mouse->Buttons & 1) && !(gMousePrevBtn & 1)) {
        GuiHandleClick(Mouse->X, Mouse->Y);
    } else if ((Mouse->Buttons & 1) && gDragWin >= 0) {
        /* PR-G10 L2：与 GuiPollMouse 统一，按住拖动时持续更新 */
        GuiDragUpdate(Mouse->X, Mouse->Y);
    } else if ((Mouse->Buttons & 1) && DesktopIconDragActive()) {
        if (gCursorVisible) {
            GfxIrqEnter();
            CursorRestore();
            GfxIrqLeave();
        }
        DesktopIconDragUpdate(Mouse->X, Mouse->Y);
    }
    if (!(Mouse->Buttons & 1) && (gMousePrevBtn & 1)) {
        int WasIconDrag = DesktopIconDragActive();

        GuiDragEnd();
        DesktopIconDragEnd();
        if (WasIconDrag) {
            GfxIrqEnter();
            CursorPaint();
            GfxPresent();
            GfxIrqLeave();
        }
    }
    /* PR-I3：右键按下边沿 → 占位回调（bit1） */
    if ((Mouse->Buttons & 2) && !(gMousePrevBtn & 2)) {
        GuiRightClickPlaceholder(Mouse->X, Mouse->Y);
    }

    /* 边沿处理后再 Move/悬停，避免 Sync 抹掉 Settings 按下高亮 */
    GuiPointerMove(Mouse->X, Mouse->Y);

    /* PR-I2：滚轮 — Files 列表 / Shell 客户区；其它忽略 */
    if (Mouse->Wheel != 0) {
        if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiOnWheel(Mouse->Wheel);
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            ConsoleOnWheel(Mouse->Wheel);
        }
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

    /* PR-S-input-drain：drain 由 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）负责；此处只 dequeue */
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
            int WasIconDrag = DesktopIconDragActive();

            GuiDragEnd();
            DesktopIconDragEnd();
            if (WasIconDrag) {
                GfxIrqEnter();
                CursorPaint();
                GfxPresent();
                GfxIrqLeave();
            }
        }
        if ((Raw.Buttons & 2) && !(LastBtn & 2)) {
            GuiRightClickPlaceholder(X, Y);
        }
        /* Settings/Store：仅按键边沿逐包；位移悬停合并到队尾 GuiPointerMove */
        if (gDragWin < 0 && ((Raw.Buttons ^ LastBtn) & 1u)) {
            if (GuiFocusKind() == GUI_WIN_SETTINGS) {
                SettingsUiOnPointer(X, Y, Raw.Buttons);
            } else if (GuiFocusKind() == GUI_WIN_STORE) {
                StoreUiOnPointer(X, Y, Raw.Buttons);
            }
        }
        LastBtn = Raw.Buttons;
    }
    if (NeedMove) {
        if ((LastBtn & 1) && gDragWin >= 0) {
            gCursorX = LastX;
            gCursorY = LastY;
            GuiDragUpdate(LastX, LastY);
        } else if ((LastBtn & 1) && DesktopIconDragActive()) {
            if (gCursorVisible) {
                GfxIrqEnter();
                CursorRestore();
                GfxPresent();
                GfxIrqLeave();
            }
            gCursorX = LastX;
            gCursorY = LastY;
            DesktopIconDragUpdate(LastX, LastY);
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
    DesktopTickClock();
    /* 按钮抬起后排队的 Store 作业在此执行，OnPointer 内不再同步拷贝/删文件 */
    StoreUiPump();
}

/*
 * Store 长 IO：继续挪光标；吞掉按键边沿（写入 gMousePrevBtn），
 * 避免卸装结束后突然触发一次「假抬起」。
 */
void GuiPollMouseMotion(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 X;
    UINT32 Y;
    int Any = 0;

    if (gInputLocked) {
        while (HalMouseDequeue(&Raw)) {
            gMousePrevBtn = Raw.Buttons;
            gCursorBtn = Raw.Buttons;
        }
        return;
    }

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gScreenWidth ? gScreenWidth : 1024;
    }
    if (Sh == 0) {
        Sh = gScreenHeight ? gScreenHeight : 768;
    }

    /* PR-S-input-drain：drain 由 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）负责；此处只 dequeue */
    X = gCursorX;
    Y = gCursorY;
    while (HalMouseDequeue(&Raw)) {
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
        gCursorBtn = Raw.Buttons;
        gMousePrevBtn = Raw.Buttons;
        Any = 1;
    }
    if (Any) {
        GuiPointerMove(X, Y);
    }
}
