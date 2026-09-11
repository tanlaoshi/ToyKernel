/*
 * XhciHid.c — PR-H-xhci-split-5：HID 配置解析 / 中断端点 / GET_REPORT
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"

/* HID GET_REPORT(Input)：复合设备键盘中断 IN 不完成时的 EP0 兜底 */
int HidGetInputReport(UINT8 Iface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0xA1,
        .bRequest = 0x01,
        .wValue = 0x0100,
        .wIndex = Iface,
        .wLength = Length
    };
    if (gSlotId == 0) {
        return -1;
    }
    gXferSlot = gSlotId;
    return ControlXfer(&Setup, Data);
}

/*
 * 真机复合：PHOTO 上键 EP(DCI=5) 从不完成、鼠 DCI=3 正常。
 * 在 Drain 释锁后轮询 GET_REPORT；与中断鼠并行，勿持 gHidQueueLock。
 */
void XhciPollKbdGetReport(void) {
    UINT8 Buf[8];
    int i;
    int Diff;

    if (!gKbdPollReport || gSlotId == 0) {
        return;
    }
    if (gGetReportBusy) {
        return;
    }
    gGetReportBusy = 1;
    ZeroMemory(Buf, sizeof(Buf));
    gXferFast = 1;
    if (HidGetInputReport(gKbdIface, Buf, 8) < 0) {
        gXferFast = 0;
        gGetReportFails++;
        if (gGetReportFails == 1) {
            BootLog("boot: xhci get-report stall/retry\n");
        }
        if (gGetReportFails >= 32) {
            gKbdPollReport = 0;
            BootLog("boot: xhci kbd get-report give up\n");
        }
        gGetReportBusy = 0;
        return;
    }
    gXferFast = 0;
    gGetReportFails = 0;
    Diff = 0;
    for (i = 0; i < 8; i++) {
        if (Buf[i] != gKbdReportPrev[i]) {
            Diff = 1;
            break;
        }
    }
    if (Diff) {
        for (i = 0; i < 8; i++) {
            gKbdReportPrev[i] = Buf[i];
            gReportBuf[i] = Buf[i];
        }
        FlushDma(gReportBuf, sizeof(gReportBuf));
        SpinLockAcquire(&gHidQueueLock);
        KbdPush();
        gStatKbdPush++;
        gStatIntrEvt++;
        SpinLockRelease(&gHidQueueLock);
    }
    gGetReportBusy = 0;
}

/*
 * 曾有 ReAddKbdIntrOnly / RecoverKbdIntr / EnableKbdGetReport：
 * GET_REPORT 易 Stall；recover 未再挂入枚举路径 → 已删，消 unused 警告。
 * HID GET_REPORT 曾作 poll 兜底；持 gHidQueueLock 时调用会死锁，故已从 Drain 移除。
 */

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
        EnumWhy("boot: why=cfg ep\n");
        if (MouseEpAddr != 0) {
            gMouseIntrDci = 0;
            gMouseEpAddr = 0;
        }
        return 0;
    }
    if (MouseEpAddr != 0) {
        gMouseSlotId = gSlotId;
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        BootLog("boot: xhci mouse with-kbd cfg ok\n");
    }
    {
        char Line[64];
        char Hex[12];
        int n = 0;
        const char *P = "boot: xhci kbd ep=";
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
 * 在首次 ConfigEP 前：从当前配置描述符认领复合鼠标 iface（SetInterface/Protocol/Idle）。
 * 成功则写出 EP 参数供 ConfigureIntr 一次 Add。
 */
int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KbdIface, UINT8 KbdEp,
                              UINT8 *EpOut, UINT16 *MpsOut, UINT8 *IvOut) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 CurAlt = 0, BestAlt = 0;
    UINT16 Off;

    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        return 0;
    }
    if (Iface == KbdIface) {
        return 0;
    }
    if ((EpAddr & 0x0F) == (KbdEp & 0x0F) && ((EpAddr ^ KbdEp) & 0x80) == 0) {
        return 0;
    }

    Off = 0;
    while (Off + 9 <= Total) {
        UINT8 Len = gCtrlBuf[Off];
        UINT8 Type = gCtrlBuf[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurAlt = gCtrlBuf[Off + 3];
            if (gCtrlBuf[Off + 2] == Iface && gCtrlBuf[Off + 5] == 3 &&
                gCtrlBuf[Off + 7] != 1) {
                BestAlt = CurAlt;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    gMousePort = gPort1;
    gMouseIface = Iface;
    (void)SetInterface(Iface, BestAlt);
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    (void)SetIdle(Iface);

    *EpOut = EpAddr;
    *MpsOut = Mps;
    *IvOut = Interval;
    {
        char Line[72];
        char Hex[12];
        int n = 0;
        const char *P = "boot: xhci mouse cfg i=";
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Iface, 2);
        P = Hex;
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        P = " p=";
        while (*P && n < 44) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gMouseIfaceProto, 2);
        P = Hex;
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        P = " ep=";
        while (*P && n < 54) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EpAddr, 2);
        P = Hex;
        while (*P && n < 58) {
            Line[n++] = *P++;
        }
        P = " mps=";
        while (*P && n < 64) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Mps, 2);
        P = Hex;
        while (*P && n < 68) {
            Line[n++] = *P++;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
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
        ToyLogUsb("boot: xhci sync deq fail\n");
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

int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                       UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 CurScore = 0;
    UINT8 BestScore = 0;
    UINT8 CurIface = 0;
    *Iface = 0;
    *EpAddr = 0;
    *Mps = 8;
    *Interval = 10;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            UINT8 Sub = Cfg[Off + 6];
            UINT8 Proto = Cfg[Off + 7];
            CurScore = 0;
            /* 3/1/1 boot keyboard 最优；3/1/0 次之；3/0/x 亦试（真机常见） */
            if (Class == 3 && Sub == 1 && Proto == 1) {
                CurScore = 3;
            } else if (Class == 3 && Sub == 1 && Proto == 0) {
                CurScore = 2;
            } else if (Class == 3 && Sub != 1) {
                CurScore = 1;
            }
            CurIface = Cfg[Off + 2];
        } else if (Type == 5 && Len >= 7 && CurScore) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                *Iface = CurIface;
                *EpAddr = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                *Interval = Cfg[Off + 6];
                (void)Speed;
                if (BestScore == 3) {
                    return 1;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    gKbdParseScore = BestScore;
    return BestScore != 0;
}

/*
 * 真机：罗技 G102 等游戏鼠常带额外 HID（媒体/宏，3/0/x 或 3/1/0），
 * 旧逻辑会当成「键盘」占 slot，再把同设备 boot 鼠绑成 composite →
 * 真键盘口永远轮不到，且假键盘 k=0、鼠却丝滑。
 * 若本设备「键盘分」<3 且已有像样鼠标接口 → 不当键盘，留给 InitMouseOnPort。
 */
int RealPcRejectMouseExtraAsKeyboard(UINT16 Total, UINT8 Speed) {
    UINT8 MIface = 0, MEp = 0, MIv = 10;
    UINT16 MMps = 8;

    if (HalCpuIsHypervisor()) {
        return 0;
    }
    if (gKbdParseScore >= 3) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &MIface, &MEp, &MMps, &MIv)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }
    BootLog("boot: xhci skip mouse+extraHID as kbd\n");
    {
        char Line[48];
        int n = 0;
        const char *P = "boot: xhci kbd-score=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gKbdParseScore % 10));
        P = " mouse-score=";
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gMouseParseScore % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    return 1;
}

/*
 * v8：G102 等被 skip-as-kbd 时，slot 已 Address 在 gDevCtx/gEp0。
 * 勿 Disable+再 Address（真机常 cc=0x04 / Reset 超时 → m=0）。
 * 直接把当前 slot 认领为独立鼠标。
 */
int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                                     UINT8 ConfigVal) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT32 Slot;

    if (HalCpuIsHypervisor() || gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }

    Slot = gSlotId;
    CopyMemory(gMouseDevCtx, gDevCtx, sizeof(gMouseDevCtx));
    FlushDma(gMouseDevCtx, sizeof(gMouseDevCtx));
    DcbaaSet(Slot, PointerToPhysical(gMouseDevCtx));
    DcbaaFlush();

    gMouseSlotId = Slot;
    gMousePort = RootPort;
    gMouseRoute = gKbdRoute & 0xFFFFFu;
    gMouseHubSlot = gKbdHubSlot;
    gMouseTtPort = gKbdTtPort;
    gMouseIface = Iface;
    gMouseAbsolute = 0;
    /* gSlotEp0UsesKbdRing[Slot] 保持 1：EP0 仍用 Address 时的 gEp0 */

    gSlotId = 0;
    gIntrDci = 0;
    gKbdIface = 0;
    gKbdRoute = 0;
    gKbdHubSlot = 0;
    gKbdTtPort = 0;

    gXferSlot = gMouseSlotId;
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("boot: xhci mouse claim SetConfig fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2 || gMouseIfaceProto == 0xFF) {
        SetIdle(Iface);
    }
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        BootLog("boot: xhci mouse claim ConfigEP fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    BootLogHexV("boot: xhci mouse claim score=", gMouseParseScore, 2);
    BootLog("boot: xhci-hid mouse (claim after skip-kbd)\n");
    return 1;
}

int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundIface = 0;
    UINT8 BestScore = 0;
    UINT8 BestIface = 0;
    UINT8 BestEp = 0;
    UINT16 BestMps = 8;
    UINT8 BestInterval = 10;
    UINT8 BestProto = 0xFF;
    UINT8 CurProto = 0;
    UINT8 CurSub = 0;
    UINT8 CurScore = 0;

    *Iface = 0;
    *EpAddr = 0;
    *Mps = 8;
    *Interval = 10;

    /*
     * 评分：boot mouse (3/1/2)=3；boot 子类 Proto0 (3/1/0)=2；其它 HID 非键盘=1。
     * 真机曾把 Proto=0 的附加 HID（媒体键等）当成鼠标 → EP 无报告 m=0。
     */
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            CurScore = 0;
            FoundIface = 0;
            if (Class == 3 && CurProto != 1) {
                FoundIface = 1;
                *Iface = Cfg[Off + 2];
                if (CurSub == 1 && CurProto == 2) {
                    CurScore = 3;
                } else if (CurSub == 1) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5 && Len >= 7 && FoundIface && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                BestProto = CurProto;
                BestIface = *Iface;
                BestEp = Addr;
                BestMps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                BestInterval = Cfg[Off + 6];
                if (BestScore == 3) {
                    break;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    (void)Speed;
    if (BestScore == 0) {
        return 0;
    }
    *Iface = BestIface;
    *EpAddr = BestEp;
    *Mps = BestMps;
    *Interval = BestInterval;
    gMouseIfaceProto = BestProto;
    gMouseParseScore = BestScore;
    gMouseAbsolute = (BestProto != 2 && HalCpuIsHypervisor()) ? 1 : 0;
    return 1;
}

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
            BootLog("boot: xhci mouse add-run fail, try stop+add\n");
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
                BootLog("boot: xhci mouse stop+add ok\n");
                /* 键盘曾 Stop：调用方须 Sync+Queue */
                return 2;
            }
            BootLog("boot: xhci mouse stop+add fail, drop-add\n");
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
        BootLog("boot: xhci mouse add-only ok\n");
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

