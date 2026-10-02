/*
 * XhciMouse.c — PR-S-xhcimouse-1：延迟绑定、报告队列与对外 API
 *
 * 从 XhciMouse.c 原样搬家；不改语义。无 static 提升；鼠标全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

/* 真机 PHOTO 后再绑鼠标，避免复合/hub 扫描踩键盘 EP */
void XhciInitMouseDeferred(void) {
    if (HalCpuIsHypervisor()) {
        return;
    }
    if (gMouseSlotId != 0) {
        return;
    }
    ToyLogUsb("Boot: XHCI mouse deferred start\n");
    if (gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 p = 1; p <= gMaxPorts; p++) {
            if (gSlotId != 0 && p == gPort1) {
                continue;
            }
            if (InitMouseOnPort(p)) {
                break;
            }
        }
    }
    if (gMouseSlotId == 0) {
        (void)InitMouseOnKeyboardSlot(); /* 回退：保鼠标 */
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0) {
        ToyLogUsb("Boot: XHCI mouse deferred ok\n");
    } else {
        ToyLogUsb("Boot: XHCI mouse deferred none\n");
    }
}

void MousePush(void) {
    UINT32 Next = (gMouseWriteIndex + 1) % MOUSE_Q;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    int UseAbsolute;
    UINT8 ParseLen;

    if (Next == gMouseReadIndex) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseWriteIndex];
    R->Wheel = 0;
    R->Absolute = 0;
    ParseLen = gMouseXferLen ? gMouseXferLen : gMouseReportLen;
    if (ParseLen < 3) {
        ParseLen = 3;
    }
    if (ParseLen > 8) {
        ParseLen = 8;
    }
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    /*
     * 仅枚举标了 gMouseAbsolute（QEMU tablet）才走绝对。
     * boot 相对鼠（Proto=2）禁止「高字节启发式」——短包残留曾把 dx/dy
     * 当成 16-bit 绝对坐标，Gui 再 /32767 → 光标钉死在角上（PHOTO m 涨、桌面不动）。
     */
    UseAbsolute = 0;
    if (gMouseAbsolute && gMouseIfaceProto != 2) {
        if (ParseLen >= 5 && X0 <= 32767 && Y0 <= 32767) {
            UseAbsolute = 1;
        } else if (ParseLen >= 6 && gMouseBuf[0] != 0 && X1 <= 32767 &&
                   Y1 <= 32767) {
            UseAbsolute = 2;
        }
    }

    if (UseAbsolute == 1) {
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
        R->Absolute = 1;
        if (ParseLen >= 6) {
            R->Wheel = (INT8)gMouseBuf[5];
        }
    } else if (UseAbsolute == 2) {
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
        R->Absolute = 1;
        if (ParseLen >= 7) {
            R->Wheel = (INT8)gMouseBuf[6];
        }
    } else {
        /* HID boot 相对鼠标：b0 buttons, b1 X, b2 Y, b3 wheel */
        int Dx = (int)(signed char)gMouseBuf[1];
        int Dy = (int)(signed char)gMouseBuf[2];
        UINT32 Sw = 0;
        UINT32 Sh = 0;
        int MaxX;
        int MaxY;

        HalVideoGetSize(&Sw, &Sh);
        if (Sw == 0) {
            Sw = 1024;
        }
        if (Sh == 0) {
            Sh = 768;
        }
        MaxX = (int)(Sw > 0 ? Sw - 1 : 0);
        MaxY = (int)(Sh > 0 ? Sh - 1 : 0);
        /* 首次相对报告：落到屏心，勿沿用 BSS 默认 512×384（大分辨率会偏） */
        if (!gMouseAbsInit) {
            gMouseAbsX = (int)(Sw / 2);
            gMouseAbsY = (int)(Sh / 2);
            gMouseAbsInit = 1;
        }
        gMouseAbsX += Dx;
        gMouseAbsY += Dy;
        if (gMouseAbsX < 0) {
            gMouseAbsX = 0;
        }
        if (gMouseAbsY < 0) {
            gMouseAbsY = 0;
        }
        if (gMouseAbsX > MaxX) {
            gMouseAbsX = MaxX;
        }
        if (gMouseAbsY > MaxY) {
            gMouseAbsY = MaxY;
        }
        R->X = (UINT32)gMouseAbsX;
        R->Y = (UINT32)gMouseAbsY;
        R->Buttons = gMouseBuf[0] & 7;
        if (ParseLen >= 4) {
            R->Wheel = (INT8)gMouseBuf[3];
        }
    }
    gMouseWriteIndex = Next;
}

int XhciMousePresent(void) {
    return gMouseSlotId != 0;
}

/*
 * PHOTO 里 HalInputPoll 只 Push 不消费 → 鼠队列易满。
 * 进桌面前只抽空队列并对齐 Abs；勿 SyncIntrDequeue（枚举后多余 Stop 曾致 PHOTO r=0）。
 */
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    int MaxX;
    int MaxY;

    /*
     * 真机相对鼠必须同步 Abs；QEMU tablet 不读 Abs，但排空队列可丢掉
     * 改 scale / 热切前积压的旧坐标。勿再对 hypervisor 早退。
     */
    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = 1024;
    }
    if (Sh == 0) {
        Sh = 768;
    }
    MaxX = (int)(Sw - 1);
    MaxY = (int)(Sh - 1);
    SpinLockAcquire(&gHidQueueLock);
    gMouseReadIndex = gMouseWriteIndex;
    gMouseAbsX = (int)CursorX;
    gMouseAbsY = (int)CursorY;
    gMouseAbsInit = 1;
    if (gMouseAbsX < 0) {
        gMouseAbsX = 0;
    }
    if (gMouseAbsY < 0) {
        gMouseAbsY = 0;
    }
    if (gMouseAbsX > MaxX) {
        gMouseAbsX = MaxX;
    }
    if (gMouseAbsY > MaxY) {
        gMouseAbsY = MaxY;
    }
    SpinLockRelease(&gHidQueueLock);
    ToyLogUsb("Boot: XHCI mouse handoff desktop\n");
}

int XhciDequeueMouse(USB_MOUSE_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gHidQueueLock);
    if (gMouseReadIndex != gMouseWriteIndex) {
        *Report = gMouseQ[gMouseReadIndex];
        gMouseReadIndex = (gMouseReadIndex + 1) % MOUSE_Q;
        Ok = 1;
    }
    SpinLockRelease(&gHidQueueLock);
    return Ok;
}

