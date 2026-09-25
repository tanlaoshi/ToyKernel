/*
 * XhciDevice.c — PR-S-xhcidevice-1：Address / Slot / Ep0 环选择
 *
 * 从 XhciDevice.c 原样搬家；不改语义。无 static 提升。
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
    } else if (SlotId != 0 && SlotId == gFtdiSlot) {
        XhciFtdiEp0Ring(RingOut, StOut);
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
    } else if (SlotOut == &gFtdiSlot) {
        XhciFtdiEp0Ring(RingOut, StOut);
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
        BootLogHex("Boot: XHCI EnableSlot cc=", gCmdCode, 2);
        BootLogHex("Boot: XHCI EnableSlot slot=", *SlotOut, 2);
        BootLogHex("Boot: XHCI EnableSlot done=", gCmdDone, 1);
        EnumWhy("Boot: Why=enable slot\n");
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
        BootLogHex("Boot: XHCI addr cc=", gCmdCode, 2);
        EnumWhy("Boot: Why=address fail\n");
        if (gXhciCmdSick && SlotOut) {
            *SlotOut = 0;
        }
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
    if (gFtdiSlot == SlotId) {
        gFtdiSlot = 0;
        gFtdiClaimed = 0;
        gFtdiPort = 0;
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

