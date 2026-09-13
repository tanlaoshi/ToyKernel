/*
 * XhciController.c — PR-H-xhci-core-split-5：TakeLegacy / Halt* / Reset / Start / BootMarkRs
 *
 * 从 XhciCore.c 原样搬家；不改语义。不抽 XhciInit。控制器相关全局仍在 XhciCore.c。
 */
#include "XHCI/XhciInternal.h"

/* 释放 USB 传统支持（BIOS 移交） */
void TakeLegacy(void) {
    UINT32 Hcc1 = ReadMmio32(gCapabilityBase + 0x10);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFF;
    int Wait;
    UINT32 After;

    if (Xecp == 0) {
        DiagChkStr("TakeLegacy", 0, "xECP!=0", "xECP=0");
        return;
    }
    UINT64 Ptr = gCapabilityBase + (UINT64)Xecp * 4;
    for (int i = 0; i < 64; i++) {
        UINT32 Val = ReadMmio32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFF);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFF);
        if (Id == 1) {
            WriteMmio32(Ptr, Val | (1u << 24)); /* OS Owned */
            Wait = HalCpuIsHypervisor() ? 1000000 : 50000;
            (void)WaitClear(Ptr, (1u << 16), Wait); /* BIOS Owned */
            After = ReadMmio32(Ptr);
            /* want: BIOS Owned(bit16)=0；got=完整 USBLEGSUP */
            DiagChk("TakeLegacy", !(After & (1u << 16)), "BIOS_OWN=0", After, 8);
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCapabilityBase + (UINT64)Next * 4;
    }
    /* 真机常见无 USBLEGSUP：安静模式不刷 FAIL */
    if (DiagVerbose()) {
        DiagChkStr("TakeLegacy", 0, "USBLEGSUP id=1", "not found");
    }
}

/* 真机：BootMark 直写帧缓冲（不 Present）；QEMU 正常串口/GOP */

/* 停 RS，避免无 HID 时事件环/遗留状态拖死后续 */
void HaltControllerQuiet(void) {
    UINT32 Cmd;

    if (gOperationalBase == 0) {
        return;
    }
    Cmd = ReadMmio32(gOperationalBase);
    Cmd &= ~USBCMD_RS;
    WriteMmio32(gOperationalBase, Cmd);
    (void)WaitSet(gOperationalBase + 4, USBSTS_HCH, 200000);
    /* 清 EINT；Interrupter 关 IE，降未路由 IRQ 风险 */
    WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 0);
    }
}

/* 复位 xHCI 控制器 */
int ResetController(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    Cmd &= ~USBCMD_RS;
    WriteMmio32(gOperationalBase, Cmd);
    if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        ToyLogUsb("boot: xhci halt timeout\n");
        DebugWrite("XHCI: halt timeout\n");
        return 0;
    }
    WriteMmio32(gOperationalBase, USBCMD_HCRST);
    if (!WaitClear(gOperationalBase, USBCMD_HCRST, 1000000) || !WaitClear(gOperationalBase + 4, USBSTS_CNR, 1000000)) {
        ToyLogUsb("boot: xhci reset timeout\n");
        DebugWrite("XHCI: reset timeout\n");
        return 0;
    }
    return 1;
}

/* 真机：只停 RS，不做 HCRST（该机 HCRST 后再 set RS 会挂） */
int HaltOnly(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    if (Cmd & USBCMD_RS) {
        WriteMmio32(gOperationalBase, Cmd & ~USBCMD_RS);
        if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 200000)) {
            return 0;
        }
    }
    return 1;
}

/* 真机：分阶 set RS 黄字（仅 VERBOSE） */
void BootMarkRs(char Kind, char Stage) {
    char Msg[32];
    int n = 0;
    const char *P = "boot: xhci ";
    if (!DiagVerbose()) {
        return;
    }
    while (*P) {
        Msg[n++] = *P++;
    }
    Msg[n++] = Kind; /* 'b' or 'a' */
    Msg[n++] = 'R';
    Msg[n++] = 'S';
    Msg[n++] = ' ';
    Msg[n++] = Stage;
    Msg[n++] = '\n';
    Msg[n] = 0;
    ToyBootMarkUsb(Msg);
}

/* 分配 DCBAA、建环并 Run 控制器 */
int StartController(UINT32 MaxSlots) {
    UINT32 Hcs2 = ReadMmio32(gCapabilityBase + 0x08);
    /* Linux HCS_MAX_SCRATCHPAD：Hi@25:21，Lo@31:27 */
    UINT32 Scratch = (((Hcs2 >> 21) & 0x1F) << 5) | ((Hcs2 >> 27) & 0x1F);
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Sts;
    UINT32 Hcc1;
    char B[12];
    UINT32 Si;

    Hcc1 = ReadMmio32(gCapabilityBase + 0x10);
    gDcbaaLive = gDcbaa;
    gDcbaaMaxSlot = MaxSlots;
    gDcbaaFromFirmware = 0;

    if (RealPc) {
        UINT64 FwDcbaap;
        UINTN MapBytes;
        UINT32 Slot;

        /*
         * 真机：固件 DCBAAP/scratch 保留（自建 DCBAAP 曾致 RS 挂）；
         * 命令环+事件环用私有（固件事件环无法可靠吃到 EnableSlot 完成）。
         */
        (void)Scratch;
        (void)Hcc1;
        FwDcbaap = gFwDcbaapSave ? gFwDcbaapSave
                                 : (ReadMmio64(gOperationalBase + 0x30) & ~0x3FULL);
        if (FwDcbaap == 0) {
            ToyBootMarkUsb("boot: xhci fw DCBAAP=0\n");
            return 0;
        }
        MapBytes = (UINTN)(MaxSlots + 1) * sizeof(UINT64);
        if (MapBytes < 0x1000) {
            MapBytes = 0x1000;
        }
        if (MapXhciDma(FwDcbaap, MapBytes) != 0) {
            ToyBootMarkUsb("boot: xhci map DCBAAP fail\n");
            return 0;
        }
        gDcbaaLive = (UINT64 *)(UINTN)FwDcbaap;
        gDcbaaFromFirmware = 1;
        WriteMmio32(gOperationalBase + 0x38, MaxSlots);
        for (Slot = 1; Slot <= MaxSlots; Slot++) {
            gDcbaaLive[Slot] = 0;
        }
        DcbaaFlush();
        WriteMmio64(gOperationalBase + 0x30, FwDcbaap);
        BootMarkV("boot: xhci use fw DCBAAP\n");

        /*
         * 真机：DCBAAP 必须固件（否则 RS 挂）；命令/事件环改私有。
         */
        (void)gFwCrcrSave;
        (void)gFwErstbaSave;
        (void)gFwEvtSave;
        (void)gFwErdpSave;
        gCmdRingLive = gCmdRing;
        gEvtRingLive = gEvtRing;
        gEvtRingSize = EVT_SIZE;
        InitRing(gCmdRing, &gCmd, RING_SIZE);
        FlushDma(gCmdRing, sizeof(gCmdRing));
        (void)MapXhciDma(PointerToPhysical(gCmdRing), sizeof(gCmdRing));
        WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1ULL);

        ZeroMemory(gEvtRing, sizeof(gEvtRing));
        gEvtDeq = 0;
        gEvtCcs = 1;
        ZeroMemory(gErst, sizeof(gErst));
        *(UINT64 *)(void *)gErst = PointerToPhysical(gEvtRing);
        *(UINT16 *)(void *)(gErst + 8) = (UINT16)EVT_SIZE;
        FlushDma(gEvtRing, sizeof(gEvtRing));
        FlushDma(gErst, sizeof(gErst));
        (void)MapXhciDma(PointerToPhysical(gEvtRing), sizeof(gEvtRing));
        (void)MapXhciDma(PointerToPhysical(gErst), sizeof(gErst));
        WriteMmio32(gRuntimeBase + 0x20, 0);
        WriteMmio32(gRuntimeBase + 0x24, 0);
        WriteMmio32(gRuntimeBase + 0x28, 1);
        WriteMmio32(gRuntimeBase + 0x2C, 0);
        WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
        WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));

        Fence();
        BootMarkRs('b', 'R');
        WriteMmio32(gOperationalBase, USBCMD_RS);
        Fence();
        BootMarkRs('a', 'R');
        if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 100000)) {
            DiagChk("StartController.fwRS", 0, "HCH=0", ReadMmio32(gOperationalBase + 4), 8);
            BootLog("boot: xhci run timeout\n");
            return 0;
        }
        if (!WaitSet(gOperationalBase + 0x18, CRCR_CRR, 100000)) {
            if (DiagVerbose()) {
                DiagChk("StartController.CRR", 0, "CRR=1", ReadMmio32(gOperationalBase + 0x18), 8);
                ToyBootMarkUsb("boot: xhci CRR TO\n");
            }
        } else {
            DiagChk("StartController.fwRS", 1, "HCH=0+CRR", ReadMmio32(gOperationalBase + 4), 8);
        }
        BootMarkV("boot: xhci RS running\n");
        return 1;
    }

    ZeroMemory(gDcbaa, sizeof(gDcbaa));
    ZeroMemory(gDevCtx, sizeof(gDevCtx));
    if (Scratch > 0) {
        if (Scratch > XHCI_SCRATCH_MAX) {
            BootLog("boot: xhci scratchpad >max\n");
            return 0;
        }
        BootLog("boot: xhci scratchpad=");
        HalSerialFormatHex(B, Scratch, 4);
        ToyLogUsb(B);
        ToyLogUsb("\n");
        ZeroMemory(gScratchPtr, sizeof(gScratchPtr));
        for (Si = 0; Si < Scratch; Si++) {
            gScratchPtr[Si] = PointerToPhysical(gScratchBuf[Si]);
            if (!(Hcc1 & 1u) && (gScratchPtr[Si] >> 32)) {
                return 0;
            }
            FlushDma(gScratchBuf[Si], 4096);
        }
        gDcbaa[0] = PointerToPhysical(gScratchPtr);
        FlushDma(gScratchPtr, sizeof(UINT64) * Scratch);
        BootLog("boot: xhci scratch ptrs ok\n");
    }

    BootLog("boot: xhci prog CONFIG/DCBAAP\n");
    WriteMmio32(gOperationalBase + 0x38, MaxSlots);
    FlushDma(gDcbaa, sizeof(gDcbaa));
    WriteMmio64(gOperationalBase + 0x30, PointerToPhysical(gDcbaa));

    BootLog("boot: xhci prog CRCR\n");
    InitRing(gCmdRing, &gCmd, RING_SIZE);
    FlushDma(gCmdRing, sizeof(gCmdRing));
    WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1);

    BootLog("boot: xhci prog ERST\n");
    ZeroMemory(gEvtRing, sizeof(gEvtRing));
    gEvtDeq = 0;
    gEvtCcs = 1;
    ZeroMemory(gErst, sizeof(gErst));
    *(UINT64 *)(void *)gErst = PointerToPhysical(gEvtRing);
    *(UINT16 *)(void *)(gErst + 8) = EVT_SIZE;
    FlushDma(gEvtRing, sizeof(gEvtRing));
    FlushDma(gErst, sizeof(gErst));

    WriteMmio32(gRuntimeBase + 0x20, 3);
    WriteMmio32(gRuntimeBase + 0x24, 0);
    WriteMmio32(gRuntimeBase + 0x28, 1);
    WriteMmio32(gRuntimeBase + 0x2C, 0);
    WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
    WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));

    Fence();
    Sts = ReadMmio32(gOperationalBase + 4);
    BootLog("boot: xhci USBSTS before RS=");
    HalSerialFormatHex(B, Sts, 8);
    ToyLogUsb(B);
    ToyLogUsb("\n");
    ToyBootMarkUsb("boot: xhci before RS\n");
    WriteMmio32(gOperationalBase, USBCMD_RS | USBCMD_INTE);
    Fence();
    ToyBootMarkUsb("boot: xhci after RS\n");

    if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        Sts = ReadMmio32(gOperationalBase + 4);
        DiagChk("StartController.RS", 0, "HCH=0", Sts, 8);
        BootLog("boot: xhci run timeout\n");
        return 0;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    DiagChk("StartController.RS", !(Sts & USBSTS_HCH), "HCH=0 running", Sts, 8);
    ToyBootMarkUsb("boot: xhci RS running\n");
    (void)gDcbaaFromFirmware;
    return 1;
}
