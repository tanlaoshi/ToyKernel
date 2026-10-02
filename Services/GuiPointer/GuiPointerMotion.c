/*
 * GuiPointerMotion.c — Store 长 IO 期间仅挪光标（PR-S3-guipointer-1）
 *
 * 从 GuiPointer.c 原样搬家；不改语义。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"

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
    if (GuiPresentBlocked()) {
        GuiPollHoldDrain(Sw, Sh);
        return;
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
