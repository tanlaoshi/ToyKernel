/*
 * XhciHidIntrMouse.c — PR-S-xhcihid-1：鼠标中断端点配置 / 入队
 * 从 XhciHid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                              UINT8 Speed) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    UINT32 CtxEntries;
    UINT32 Route = 0;
    UINT32 RootPort;
    UINT8 HubSlot = 0;
    UINT8 TtPort = 0;
    int Composite;
    int AddOnly;

    gMouseIntrDci = (UINT32)EpNum * 2 + In;
    gMouseEpAddr = EpAddr;
    if (Mps == 0 || Mps > 64) {
        Mps = 8;
    }
    /* 与键盘一致：TRB 长度用 8+ISP；gMouseReportLen 仅作解析上限 */
    gMouseReportLen = (UINT8)(Mps > 8 ? 8 : Mps);
    if (gMouseReportLen < 3) {
        gMouseReportLen = 3;
    }

    CtxEntries = gMouseIntrDci;
    RootPort = gMousePort;
    Composite = (SlotId == gSlotId && gSlotId != 0);
    AddOnly = 0;

    /*
     * 复合设备：只 Add 鼠标，勿 Drop/重建键盘 EP（Linux：各 iface 独立）。
     * NUC PHOTO：Stop+Sync 后仍 k=0、m 正常 → 不要 Stop 键盘；
     * Running 时 Add 若 ConfigEP 失败再回退 Stop+Add-only / Drop+Add。
     */
    if (Composite) {
        if (gIntrDci > CtxEntries) {
            CtxEntries = gIntrDci;
        }
        Route = gKbdRoute & 0xFFFFFu;
        RootPort = gPort1;
        HubSlot = gKbdHubSlot;
        TtPort = gKbdTtPort;
        Speed = gSpeed;
        AddOnly = 1;
    } else {
        /* 独立鼠标（含 hub 子口）：ConfigEP 必须带回 Address 时的 Route/TT */
        Route = gMouseRoute & 0xFFFFFu;
        HubSlot = gMouseHubSlot;
        TtPort = gMouseTtPort;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    if (Composite && gIntrDci != 0 && AddOnly) {
        /* 不 Drop 键盘；只 Add slot + 鼠标 DCI，抬高 Context Entries */
        *(UINT32 *)(void *)(gInCtx + 0) = 0;
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    } else if (Composite && gIntrDci != 0) {
        *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
        *(UINT32 *)(void *)(gInCtx + 4) =
            (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
    } else {
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
    Slot[1] = (UINT32)RootPort << 16;
    if (HubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
    }

    /* Drop+Add 回退路径才重填键盘 EP；Add-only 保持首次 ConfigureIntr 的上下文 */
    if (Composite && gIntrDci != 0 && !AddOnly) {
        UINT32 *KbdEp = (UINT32 *)(void *)InEp(gIntrDci);
        UINT8 KbdIv;
        UINT16 KbdMps;
        UINT64 KbdDeq;

        KbdMps = gKbdMps;
        if (KbdMps == 0 || KbdMps > 64) {
            KbdMps = 8;
        }
        KbdIv = gKbdEpInterval;
        if (KbdIv == 0) {
            KbdIv = (Speed >= 3) ? 3 : FsInterval(10);
        }
        InitRing(gIntrRing, &gIntr, RING_SIZE);
        KbdEp[0] = (UINT32)KbdIv << 16;
        KbdEp[1] = (3u << 1) | (7u << 3) | ((UINT32)KbdMps << 16);
        KbdDeq = PointerToPhysical(gIntrRing) | 1;
        KbdEp[2] = (UINT32)KbdDeq;
        KbdEp[3] = (UINT32)(KbdDeq >> 32);
        KbdEp[4] = (UINT32)KbdMps | ((UINT32)KbdMps << 16);
        FlushDma(gIntrRing, sizeof(gIntrRing));
    }

    InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0)
                                  : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        if (Composite && AddOnly && gIntrDci != 0) {
            UINT32 EpField = (gIntrDci & 0x1Fu) << 16;

            /*
             * Running 时 Add 失败：再试 Stop 后 Add-only（旧 NUC 经验）。
             * 仍失败才 Drop+Add（保鼠标，键盘可能 k=0）。
             */
            BootLog("Boot: XHCI mouse add-run fail, try stop+add\n");
            (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EpField, 0);
            ProcessEvents();
            if (!HalCpuIsHypervisor()) {
                ProcessEventsRealPc();
            }
            ZeroMemory(gInCtx, sizeof(gInCtx));
            *(UINT32 *)(void *)(gInCtx + 0) = 0;
            *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
            Slot = (UINT32 *)(void *)InSlot();
            Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
            Slot[1] = (UINT32)RootPort << 16;
            if (HubSlot != 0 && Speed < 3) {
                Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
            }
            InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
            Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
            Ep[0] = (UINT32)Interval << 16;
            Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
            Deq = PointerToPhysical(gMouseIntrRing) | 1;
            Ep[2] = (UINT32)Deq;
            Ep[3] = (UINT32)(Deq >> 32);
            Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);
            FlushDma(gInCtx, sizeof(gInCtx));
            FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
            if (Command(PointerToPhysical(gInCtx),
                        TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) == 0) {
                BootLog("Boot: XHCI mouse stop+add ok\n");
                /* 键盘曾 Stop：调用方须 Sync+Queue */
                return 2;
            }
            BootLog("Boot: XHCI mouse stop+add fail, drop-add\n");
            AddOnly = 0;
            ZeroMemory(gInCtx, sizeof(gInCtx));
            *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
            *(UINT32 *)(void *)(gInCtx + 4) =
                (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
            Slot = (UINT32 *)(void *)InSlot();
            Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
            Slot[1] = (UINT32)RootPort << 16;
            if (HubSlot != 0 && Speed < 3) {
                Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
            }
            {
                UINT32 *KbdEp = (UINT32 *)(void *)InEp(gIntrDci);
                UINT8 KbdIv = gKbdEpInterval ? gKbdEpInterval
                                             : (UINT8)((Speed >= 3) ? 3 : FsInterval(10));
                UINT16 KbdMps = (gKbdMps && gKbdMps <= 64) ? gKbdMps : 8;
                UINT64 KbdDeq;

                InitRing(gIntrRing, &gIntr, RING_SIZE);
                KbdEp[0] = (UINT32)KbdIv << 16;
                KbdEp[1] = (3u << 1) | (7u << 3) | ((UINT32)KbdMps << 16);
                KbdDeq = PointerToPhysical(gIntrRing) | 1;
                KbdEp[2] = (UINT32)KbdDeq;
                KbdEp[3] = (UINT32)(KbdDeq >> 32);
                KbdEp[4] = (UINT32)KbdMps | ((UINT32)KbdMps << 16);
                FlushDma(gIntrRing, sizeof(gIntrRing));
            }
            InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
            Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
            Ep[0] = (UINT32)Interval << 16;
            Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
            Deq = PointerToPhysical(gMouseIntrRing) | 1;
            Ep[2] = (UINT32)Deq;
            Ep[3] = (UINT32)(Deq >> 32);
            Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);
            FlushDma(gInCtx, sizeof(gInCtx));
            FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
            if (Command(PointerToPhysical(gInCtx),
                        TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
                DebugWrite("XHCI: mouse endpoint failed\n");
                return 0;
            }
            return 3; /* drop-add：调用方 Sync 键盘 */
        }
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    if (Composite && AddOnly) {
        BootLog("Boot: XHCI mouse add-only ok\n");
        return 1; /* 键盘未 Stop：勿 Sync */
    }
    return 1;
}

void QueueMouseIntr(void) {
    UINT32 Len;

    gMouseIntrDone = 0;
    gMouseReportReady = 0;
    /* 短包只写前 N 字节；不清零会让相对鼠被「高字节非0」启发式误判成绝对 */
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    FlushDma(gMouseBuf, sizeof(gMouseBuf));
    /*
     * TRB 长度不得超过 EP MPS。真机 composite 鼠 mps=4 时曾固定 enqueue 8，
     * HC 不调度完成 → PHOTO m=0 而键盘正常。
     */
    Len = gMouseReportLen;
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gMouseIntrRing, &gMouseIntr, PointerToPhysical(gMouseBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(gMouseSlotId, gMouseIntrDci);
}
