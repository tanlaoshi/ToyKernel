/*
 * XhciDevice.c — PR-H-xhci-split-4：枚举 / EP0 / 控制传输 / 描述符
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"

UINT16 SpeedMps(UINT8 Speed) {
    if (Speed == 4) {
        return 512;
    }
    if (Speed == 3) {
        return 64;
    }
    return 8;
}

/*
 * 键 / hub / 独立鼠 各用独立 EP0 环。
 * 真机证据：Address 子设备时 InitRing(共享 gEp0Ring) 会毁掉 hub EP0 dequeue，
 * hub Slot 仍 Hub=1 且鼠 epst=Running，但 TT 中断 IN 永不完成 → PHOTO m=0。
 */
void Ep0RingForSlot(UINT32 SlotId, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotId != 0 && SlotId == gMscProbeHubSlot) {
        /* 第二 hub MSC probe：Address 时已用 msc EP0 环 */
        *RingOut = gMscScanEp0Ring;
        *StOut = &gMscScanEp0;
    } else if (SlotId != 0 && SlotId == gHubSlotId) {
        *RingOut = gHubEp0Ring;
        *StOut = &gHubEp0;
    } else if (SlotId != 0 && SlotId == gMscScanSlot) {
        *RingOut = gMscScanEp0Ring;
        *StOut = &gMscScanEp0;
    } else if (SlotId != 0 && SlotId <= DCBAA_SLOTS && gSlotEp0UsesKbdRing[SlotId]) {
        /* 曾以键盘路径 Address：claim 为鼠后仍跟 gEp0 硬件 dequeue */
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    } else if (SlotId != 0 && SlotId == gMouseSlotId && SlotId != gSlotId) {
        *RingOut = gMouseEp0Ring;
        *StOut = &gMouseEp0;
    } else {
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    }
}

void Ep0RingForSlotOut(UINT32 *SlotOut, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotOut == &gHubSlotId) {
        *RingOut = gHubEp0Ring;
        *StOut = &gHubEp0;
    } else if (SlotOut == &gMouseSlotId) {
        *RingOut = gMouseEp0Ring;
        *StOut = &gMouseEp0;
    } else if (SlotOut == &gMscScanSlot) {
        *RingOut = gMscScanEp0Ring;
        *StOut = &gMscScanEp0;
    } else {
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    }
}

int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx, UINT32 RouteString,
                               UINT8 ParentHubSlot, UINT8 TtPort,
                               int HubDevice, UINT8 HubNumPorts) {
    int Ok;
    XHCI_TRB *Ep0Ring;
    RING_STATE *Ep0St;

    gXferSlot = 0;
    if (SlotOut) {
        *SlotOut = 0;
    }
    DiagChk("AddressDev.port", 1, "root+spd", ((UINT64)RootPort << 8) | Speed, 4);
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), SlotOut) < 0 || *SlotOut == 0 ||
        *SlotOut > gDcbaaMaxSlot) {
        DiagChkStr("AddressDev", 0, "EnableSlot ok", "fail");
        BootLogHex("boot: xhci EnableSlot cc=", gCmdCode, 2);
        BootLogHex("boot: xhci EnableSlot slot=", *SlotOut, 2);
        BootLogHex("boot: xhci EnableSlot done=", gCmdDone, 1);
        EnumWhy("boot: why=enable slot\n");
        return 0;
    }

    gXferSlot = *SlotOut;
    DcbaaSet(*SlotOut, PointerToPhysical(DevCtx));
    ZeroMemory(DevCtx, 2048);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    if (SlotOut == &gSlotId) {
        gKbdRoute = RouteString & 0xFFFFFu;
        gKbdHubSlot = ParentHubSlot;
        gKbdTtPort = TtPort;
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[*SlotOut] = 1;
        }
    }
    if (SlotOut == &gMouseSlotId) {
        gMouseRoute = RouteString & 0xFFFFFu;
        gMouseHubSlot = ParentHubSlot;
        gMouseTtPort = TtPort;
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[*SlotOut] = 0;
        }
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20) | (RouteString & 0xFFFFFu);
    if (HubDevice) {
        Slot[0] |= (1u << 26); /* USB2 hub only; SS hub must stay Hub=0 */
        if (gHubMtt) {
            Slot[0] |= (1u << 25); /* 仅 Multi-TT hub */
        }
    }
    Slot[1] = ((UINT32)RootPort << 16);
    if (HubDevice && HubNumPorts != 0) {
        Slot[1] |= ((UINT32)HubNumPorts << 24);
    }
    if (ParentHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)ParentHubSlot | ((UINT32)TtPort << 8);
    }

    Ep0RingForSlotOut(SlotOut, &Ep0Ring, &Ep0St);
    InitRing(Ep0Ring, Ep0St, RING_SIZE);
    UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
    gEp0Mps = SpeedMps(Speed);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEp0Mps << 16);
    UINT64 Deq = PointerToPhysical(Ep0Ring) | 1;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(DevCtx, 2048);
    DcbaaFlush();
    FlushDma(Ep0Ring, RING_SIZE * sizeof(XHCI_TRB));

    Ok = Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(*SlotOut), 0) == 0;
    DiagChk("AddressDev", Ok, "AddressDev cc=1", gCmdCode, 2);
    if (!Ok) {
        BootLogHex("boot: xhci addr cc=", gCmdCode, 2);
        EnumWhy("boot: why=address fail\n");
        return 0;
    }
    return 1;
}

/* Enable Slot + Address Device（根口设备） */
int AddressDevice(UINT32 Port1, UINT8 Speed) {
    gXferSlot = gSlotId;
    return AddressDeviceOnPort(Port1, Speed, &gSlotId, gDevCtx, 0, 0, 0, 0, 0);
}

void DisableSlot(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DCBAA_SLOTS) {
        return;
    }
    (void)Command(0, TRB_TYPE(TRB_DISABLE_SLOT) | TRB_SLOT(SlotId), 0);
    DcbaaSet(SlotId, 0);
    gSlotEp0UsesKbdRing[SlotId] = 0;
    if (gSlotId == SlotId) {
        gSlotId = 0;
    }
    if (gMouseSlotId == SlotId) {
        gMouseSlotId = 0;
    }
    if (gHubSlotId == SlotId) {
        gHubSlotId = 0;
    }
    if (gMscProbeHubSlot == SlotId) {
        gMscProbeHubSlot = 0;
    }
    if (gMscScanSlot == SlotId) {
        gMscScanSlot = 0;
        gMscClaimed = 0;
        gMscPort = 0;
        gMscBulkInDci = 0;
        gMscBulkOutDci = 0;
        gMscCapacityOk = 0;
        gMscBlockCount = 0;
        gMscBlockSize = 0;
    }
    if (gXferSlot == SlotId) {
        gXferSlot = 0;
    }
}

/* ControlXfer 超时后 EP0 环与 HC 失步，须 Reset+SetTrDeq 才能继续枚举 */
void RecoverEp0(UINT32 SlotId) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;
    UINT32 EpField = (1u << 16);
    UINT32 QuietSave;

    if (SlotId == 0) {
        return;
    }
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1; /* 恢复过程中的 Stop/ResetEP 勿刷 FAIL */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    Ep0RingForSlot(SlotId, &Ring, &St);
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RING_SIZE * sizeof(XHCI_TRB));
    Deq = PointerToPhysical(Ring) | 1;
    (void)Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    gDiagQuiet = QuietSave;
}

/* EP0 控制传输（SETUP-DATA-STATUS） */
int ControlXfer(USB_SETUP_PACKET *Setup, void *Data) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 SetupParam = 0;
    UINT8 *Raw = (UINT8 *)Setup;
    for (int i = 0; i < 8; i++) {
        SetupParam |= ((UINT64)Raw[i]) << (8 * i);
    }

    UINT32 Trt = 0;
    if (Setup->wLength && Data) {
        Trt = (Setup->bmRequestType & 0x80) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    Ep0RingForSlot(gXferSlot, &Ring, &St);

    /* 清完成码：超时后若仍显示上一笔 cc=1，会误报 FAIL want=cc=1|13 got=0x01 */
    gXferDone = 0;
    gXferCode = 0;
    Enqueue(Ring, St, SetupParam, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | Trt);

    if (Setup->wLength && Data) {
        UINT32 Dir = (Setup->bmRequestType & 0x80) ? TRB_DIR_IN : 0;
        Enqueue(Ring, St, PointerToPhysical(Data), Setup->wLength, TRB_TYPE(TRB_DATA) | Dir);
    }

    UINT32 StatusDir = (Setup->wLength && (Setup->bmRequestType & 0x80)) ? 0 : TRB_DIR_IN;
    Enqueue(Ring, St, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDoorbell(gXferSlot, 1);
    if (WaitTransfer(150000) < 0) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gXferDone && (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET)) {
            DiagChk("ControlXfer", 1, "cc=1|13", gXferCode, 2);
            return 0;
        }
        if (!gXferDone) {
            if (DiagVerbose()) {
                DiagChkStr("ControlXfer", 0, "xfer done", "timeout");
            }
        } else if (!gXferFast && gCtrlFailLogged < 2) {
            /* Stall(6) 在 GET_REPORT 轮询时很常见；限 2 条免刷屏 */
            DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
            gCtrlFailLogged++;
        }
        RecoverEp0(gXferSlot);
        return -1;
    }
    if (!(gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET)) {
        if (!gXferFast && gCtrlFailLogged < 2) {
            DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
            gCtrlFailLogged++;
        }
        return -1;
    }
    if (DiagVerbose()) {
        DiagChk("ControlXfer", 1, "cc=1|13", gXferCode, 2);
    }
    return 0;
}

/* GET_DESCRIPTOR 控制传输封装 */
int GetDesc(UINT16 TypeIndex, UINT16 Index, UINT16 Length, void *Buf) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue = TypeIndex,
        .wIndex = Index,
        .wLength = Length
    };
    ZeroMemory(Buf, Length);
    FlushDma(Buf, Length);
    if (ControlXfer(&Setup, Buf) < 0) {
        return -1;
    }
    FlushDma(Buf, Length);
    return 0;
}

/* USB2 hub：对齐 EDK2 XhcConfigHubContext —— 从 Output Slot 拷贝后 ConfigEP，写入 Hub/TTT/MTT/端口数。
 * 仅 Evaluate 且不带 TTT 时，真机常见 EP0 经 TT 成功、中断 IN 永不完成（PHOTO m=0）。 */
int EvaluateHubSlot(UINT32 SlotId, UINT32 RootPort, UINT8 Speed, UINT8 NumPorts) {
    UINT32 *InSlotCtx;
    UINT32 *OutSlotCtx;
    UINT32 i;
    UINT32 Words;

    if (Speed >= 4 || NumPorts == 0 || SlotId == 0) {
        return 0;
    }
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0); /* Add A0 */
    InSlotCtx = (UINT32 *)(void *)InSlot();
    FlushDma(gHubDevCtx, 2048);
    OutSlotCtx = (UINT32 *)(void *)gHubDevCtx;
    Words = gCtxSize / 4u;
    if (Words > 16) {
        Words = 16;
    }
    for (i = 0; i < Words; i++) {
        InSlotCtx[i] = OutSlotCtx[i];
    }
    /* Context Entries 至少 1；Hub + 可选 MTT + TTT */
    if (((InSlotCtx[0] >> 27) & 0x1Fu) < 1u) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0x1Fu << 27)) | (1u << 27);
    }
    InSlotCtx[0] |= (1u << 26);
    if (gHubMtt) {
        InSlotCtx[0] |= (1u << 25);
    } else {
        InSlotCtx[0] &= ~(1u << 25);
    }
    InSlotCtx[0] = (InSlotCtx[0] & ~(3u << 16)) | (((UINT32)gHubTtt & 3u) << 16);
    if (Speed != 0) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0xFu << 20)) | ((UINT32)Speed << 20);
    }
    InSlotCtx[1] = (InSlotCtx[1] & 0x0000FFFFu) |
                   ((UINT32)RootPort << 16) | ((UINT32)NumPorts << 24);
    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gHubDevCtx, 2048);
    /* EDK2 走 Configure Endpoint（非 Evaluate）更新 hub Slot */
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) != 0) {
        BootLogHex("boot: xhci hub cfg cc=", gCmdCode, 2);
        return 0;
    }
    BootLogHex("boot: xhci hub mtt=", gHubMtt, 1);
    BootLogHex("boot: xhci hub ttt=", gHubTtt, 1);
    return 1;
}

/* Device Desc 仍在 gCtrlBuf：HS Multi-TT hub 的 bDeviceProtocol==2 */
void HubNoteMttFromDevDesc(UINT8 Speed) {
    gHubMtt = 0;
    if (Speed == 3 && gCtrlBuf[4] == 0x09 && gCtrlBuf[7] == 2) {
        gHubMtt = 1;
    }
}

int EvaluateEp0(UINT32 SlotId, UINT16 Mps) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;

    Ep0RingForSlot(SlotId, &Ring, &St);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 1);
    {
        UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
        Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)Mps << 16);
        Deq = PointerToPhysical(&Ring[St->Enq]) | (UINT64)(St->Pcs & 1);
        Ep0[2] = (UINT32)Deq;
        Ep0[3] = (UINT32)(Deq >> 32);
    }
    gEp0Mps = Mps;
    FlushDma(gInCtx, sizeof(gInCtx));
    return Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_EVALUATE_CTX) | TRB_SLOT(SlotId), 0) == 0;
}

/* 先 8 字节拿 bMaxPacketSize0，再 18 字节完整设备描述符 */
int GetDeviceDesc(void) {
    UINT8 Mps;
    int Ok;

    Ok = GetDesc(0x0100, 0, 8, gCtrlBuf) == 0;
    if (DiagVerbose()) {
        DiagChk("GetDesc8", Ok, "xfer ok", Ok ? gCtrlBuf[7] : gXferCode, 2);
    }
    if (!Ok) {
        EnumWhy("boot: why=desc8\n");
        return -1;
    }
    Mps = gCtrlBuf[7];
    if (Mps != 8 && Mps != 16 && Mps != 32 && Mps != 64) {
        Mps = (UINT8)gEp0Mps;
    }
    if (Mps != (UINT8)gEp0Mps) {
        (void)EvaluateEp0(gXferSlot, Mps);
    }
    Ok = GetDesc(0x0100, 0, 18, gCtrlBuf) == 0;
    if (DiagVerbose()) {
        DiagChk("GetDesc18", Ok, "len>=18 class", Ok ? gCtrlBuf[4] : gXferCode, 2);
    }
    if (!Ok) {
        EnumWhy("boot: why=desc18\n");
        return -1;
    }
    return 0;
}

/* SET_CONFIGURATION 请求 */
int SetConfig(UINT8 Config) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x00,
        .bRequest = 0x09,
        .wValue = Config,
        .wIndex = 0,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_PROTOCOL Boot 协议 */
int SetProtocolBoot(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0B,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_IDLE 请求 */
int SetIdle(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0A,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_REPORT：输出报告（键盘 LED 等） */
int SetReportOutput(UINT8 Iface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x09,
        .wValue = 0x0200,
        .wIndex = Iface,
        .wLength = Length
    };
    return ControlXfer(&Setup, Data);
}

/* HID SET_INTERFACE：激活指定 Alternate（复合键鼠偶见鼠标在 alt>0） */
int SetInterface(UINT8 Iface, UINT8 Alt) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x01,
        .bRequest = 0x0B,
        .wValue = Alt,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

