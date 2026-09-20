/*
 * XhciHidIntrKbd.c — PR-S-xhcihid-1：键盘中断端点配置 / 出队 / 入队
 * 从 XhciHid.c 原样搬家；不改语义。HID 全局仍在 XhciCore.c。
 */
#include "XHCI/XhciInternal.h"

UINT8 FsInterval(UINT8 BInterval) {
    if (BInterval == 0) {
        BInterval = 1;
    }
    UINT8 Log2 = 0;
    UINT8 V = BInterval;
    while (V > 1) {
        V >>= 1;
        Log2++;
    }
    return (UINT8)(Log2 + 3);
}

/* 配置 HID 中断 IN 端点 */
/*
 * 首次 ConfigEP。MouseEpAddr!=0 时同一次 Add 键盘+鼠标（真机二次 ConfigEP 会弄死键盘，
 * 含 Add-only；NUC PHOTO：add-only ok 仍 k=0 m 正常）。
 */
int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed,
                         UINT8 MouseEpAddr, UINT16 MouseMps, UINT8 MouseBInterval) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    UINT8 Interval;
    UINT32 CtxEntries;
    UINT32 AddFlags;

    gIntrDci = (UINT32)EpNum * 2 + In;
    gKbdEpAddr = EpAddr;
    if (Mps == 0 || Mps > 64) {
        Mps = 8;
    }
    gKbdMps = Mps;

    CtxEntries = gIntrDci;
    AddFlags = (1u << 0) | (1u << gIntrDci);
    if (MouseEpAddr != 0) {
        UINT8 MEpNum = MouseEpAddr & 0x0F;
        UINT8 MIn = (MouseEpAddr & 0x80) ? 1 : 0;

        gMouseIntrDci = (UINT32)MEpNum * 2 + MIn;
        gMouseEpAddr = MouseEpAddr;
        if (MouseMps == 0 || MouseMps > 64) {
            MouseMps = 8;
        }
        gMouseReportLen = (UINT8)(MouseMps > 8 ? 8 : MouseMps);
        if (gMouseReportLen < 3) {
            gMouseReportLen = 3;
        }
        if (gMouseIntrDci > CtxEntries) {
            CtxEntries = gMouseIntrDci;
        }
        AddFlags |= (1u << gMouseIntrDci);
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = AddFlags;

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gKbdRoute & 0xFFFFFu);
    Slot[1] = (UINT32)gPort1 << 16;
    if (gKbdHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gKbdHubSlot | ((UINT32)gKbdTtPort << 8);
    }

    InitRing(gIntrRing, &gIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gIntrDci);
    Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    gKbdEpInterval = Interval;
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    /* Average TRB Length | Max ESIT Payload Lo（HID：=MPS；为 0 时部分 HC 不调度中断 IN） */
    Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);

    if (MouseEpAddr != 0) {
        UINT8 MInterval;
        UINT32 *MEp;
        UINT64 MDeq;

        InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
        MEp = (UINT32 *)(void *)InEp(gMouseIntrDci);
        MInterval = (Speed >= 3)
                        ? (UINT8)((MouseBInterval > 0) ? (MouseBInterval - 1) : 0)
                        : FsInterval(MouseBInterval);
        MEp[0] = (UINT32)MInterval << 16;
        MEp[1] = (3u << 1) | (7u << 3) | ((UINT32)MouseMps << 16);
        MDeq = PointerToPhysical(gMouseIntrRing) | 1;
        MEp[2] = (UINT32)MDeq;
        MEp[3] = (UINT32)(MDeq >> 32);
        MEp[4] = (UINT32)MouseMps | ((UINT32)MouseMps << 16);
        FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
    }

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gIntrRing, sizeof(gIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        DebugWrite("XHCI: Configure Endpoint failed\n");
        EnumWhy("Boot: Why=cfg ep\n");
        if (MouseEpAddr != 0) {
            gMouseIntrDci = 0;
            gMouseEpAddr = 0;
        }
        return 0;
    }
    if (MouseEpAddr != 0) {
        gMouseSlotId = gSlotId;
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        BootLog("Boot: XHCI mouse with-kbd cfg ok\n");
    }
    {
        char Line[64];
        char Hex[12];
        int n = 0;
        const char *P = "Boot: XHCI kbd ep=";
        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EpAddr, 2);
        P = Hex;
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    DebugWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/*
 * 真机 Arm：枚举期已挂中断 TRB。须先 Stop（环仍有效）→ 排空 Stopped 事件
 * → 再 InitRing → Set TR Dequeue；失败则 Reset EP 再试。
 * 旧序 InitRing 先于 Stop 会毁掉 HC 还在用的环，且 Stop 回调里 QueueIntr
 * 会导致 SetTrDeq 报 Context State Error (got=0x13)。
 */
int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St,
                           UINTN RingBytes) {
    UINT64 Deq;
    UINT32 EpField = (Dci & 0x1Fu) << 16;

    if (Slot == 0 || Dci == 0) {
        return -1;
    }

    /* 1) 先停 EP（此时环内容仍与硬件一致） */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(Slot) | EpField, 0);
    ProcessEvents();
    if (!HalCpuIsHypervisor()) {
        ProcessEventsRealPc();
    }

    /* 2) 软件环从头重建，再告诉 HC 新 dequeue */
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EpField, 0) == 0) {
        return 0;
    }

    /* 3) Context State 等：Reset EP 后再 SetTrDeq */
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(Slot) | EpField, 0);
    ProcessEvents();
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EpField, 0) < 0) {
        ToyLogUsb("Boot: XHCI sync deq fail\n");
        return -1;
    }
    return 0;
}

/* 提交中断 IN：长度用首次 ConfigureIntr 的 MPS（勿超过 8） */
void QueueIntr(void) {
    UINT32 Len = gKbdMps;

    gIntrDone = 0;
    gIntrReportReady = 0;
    FlushDma(gReportBuf, sizeof(gReportBuf));
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gIntrRing, &gIntr, PointerToPhysical(gReportBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(gSlotId, gIntrDci);
}
