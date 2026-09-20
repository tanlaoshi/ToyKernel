/*
 * XhciDeviceControl.c — PR-S-xhcidevice-1：ControlXfer / GetDesc / Evaluate
 *
 * 从 XhciDevice.c 原样搬家；不改语义。无 static 提升。
 */
#include "XHCI/XhciInternal.h"

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
    /* excl-1：门铃与 WaitTransfer 同独占窗 */
    XhciEventEnterExclusive();
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
            XhciEventLeaveExclusive();
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
        XhciEventLeaveExclusive();
        RecoverEp0(gXferSlot);
        return -1;
    }
    if (!(gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET)) {
        XhciEventLeaveExclusive();
        if (!gXferFast && gCtrlFailLogged < 2) {
            DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
            gCtrlFailLogged++;
        }
        return -1;
    }
    XhciEventLeaveExclusive();
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
        BootLogHex("Boot: XHCI hub cfg cc=", gCmdCode, 2);
        return 0;
    }
    BootLogHex("Boot: XHCI hub mtt=", gHubMtt, 1);
    BootLogHex("Boot: XHCI hub ttt=", gHubTtt, 1);
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
        EnumWhy("Boot: Why=desc8\n");
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
        EnumWhy("Boot: Why=desc18\n");
        return -1;
    }
    return 0;
}

