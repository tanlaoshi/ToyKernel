/*
 * GuiPointer.c — 鼠标队列轮询与输入锁（核心）
 * 辅助：GuiClick.c / GuiPointerMouse.c
 *
 * 从 GuiPointer.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
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

int gInputLocked;
UINT8 gMousePrevBtn;

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

            GuiResizeEnd();
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
        if (gDragWin < 0 && gResizeWin < 0 && ((Raw.Buttons ^ LastBtn) & 1u)) {
            if (GuiFocusKind() == GUI_WIN_SETTINGS) {
                SettingsUiOnPointer(X, Y, Raw.Buttons);
            } else if (GuiFocusKind() == GUI_WIN_STORE) {
                StoreUiOnPointer(X, Y, Raw.Buttons);
            }
        }
        LastBtn = Raw.Buttons;
    }
    if (NeedMove) {
        if ((LastBtn & 1) && gResizeWin >= 0) {
            gCursorX = LastX;
            gCursorY = LastY;
            GuiResizeUpdate(LastX, LastY);
        } else if ((LastBtn & 1) && gDragWin >= 0) {
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
