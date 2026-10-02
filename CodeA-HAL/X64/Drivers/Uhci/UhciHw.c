/*
 * UhciHw.c — 单控制器复位 / 帧表 / CCS（PR-H-uhci-1）
 */
#include "UhciPrivate.h"
#include "PhysicalMemory.h"
#include "ToySerialLog.h"
#include "HalSerial.h"

UHCI_CTRL gUhci[UHCI_MAX_CTRL];
int gUhciCount;
int gUhciReady;
UINT32 gUhciCcsOr;

void UhciDelay(int Loops) {
    while (Loops-- > 0) {
        HalCpuRelax();
    }
}

void UhciSurveyCcs(UHCI_CTRL *C) {
    UINT8 P;
    UINT16 Ps;
    UINT32 Mask = 0;

    if (!C || !C->IoBase) {
        return;
    }
    for (P = 1; P <= C->NPorts && P <= 8; P++) {
        Ps = UhciR16(C->IoBase, UHCI_PORTSC(P));
        /* 写 1 清 CSC/PEDC；保留 CCS 只读位 */
        UhciW16(C->IoBase, UHCI_PORTSC(P),
                (UINT16)((Ps & (UINT16)~(UHCI_PORT_CSC | UHCI_PORT_PEDC)) |
                         UHCI_PORT_CSC | UHCI_PORT_PEDC));
    }
    UhciDelay(200000);
    for (P = 1; P <= C->NPorts && P <= 8; P++) {
        Ps = UhciR16(C->IoBase, UHCI_PORTSC(P));
        if (Ps & UHCI_PORT_CCS) {
            Mask |= (1u << (P - 1));
        }
    }
    C->CcsMask = Mask;
}

static void LogCtrl(UHCI_CTRL *C, int Index) {
    char Hex[20];
    char Line[80];
    int n = 0;
    const char *P;

    P = "Boot: UHCI#";
    while (*P && n < 16) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + (Index % 10));
    P = " Io=";
    while (*P && n < 24) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, C->IoBase, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    P = " Ccs=";
    while (*P && n < 40) {
        Line[n++] = *P++;
    }
    HalSerialFormatHex(Hex, C->CcsMask, 4);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n++] = Hex[4];
    Line[n++] = Hex[5];
    Line[n++] = '\n';
    Line[n] = 0;
    ToyBootMarkUsb(Line);
}

int UhciInitOne(UHCI_CTRL *C, int Index) {
    UINT32 *Fl;
    UINT32 i;
    int Spin;

    if (!C || C->Pci.BaseAddress == 0 || C->Pci.BaseAddress > 0xFFFFu) {
        return 0;
    }
    C->IoBase = (UINT16)C->Pci.BaseAddress;
    C->NPorts = UHCI_NPORTS;
    C->Up = 0;
    C->CcsMask = 0;
    C->FrameList = 0;

    /* HCRESET */
    UhciW16(C->IoBase, UHCI_USBCMD, UHCI_CMD_HCRESET);
    Spin = 200000;
    while (Spin-- > 0) {
        if ((UhciR16(C->IoBase, UHCI_USBCMD) & UHCI_CMD_HCRESET) == 0) {
            break;
        }
        HalCpuRelax();
    }
    if (UhciR16(C->IoBase, UHCI_USBCMD) & UHCI_CMD_HCRESET) {
        ToyBootMarkUsb("Boot: UHCI Reset Timeout\n");
        return 0;
    }

    Fl = (UINT32 *)PhysicalMemoryAllocatePages(1);
    if (!Fl) {
        ToyBootMarkUsb("Boot: UHCI FrameList Alloc Fail\n");
        return 0;
    }
    for (i = 0; i < UHCI_FRAME_ENTRIES; i++) {
        Fl[i] = UHCI_LINK_TERMINATE;
    }
    C->FrameList = Fl;
    UhciW32(C->IoBase, UHCI_FLBASEADD, (UINT32)(UINTN)Fl);
    UhciW16(C->IoBase, UHCI_FRNUM, 0);
    UhciW16(C->IoBase, UHCI_USBINTR, 0);
    UhciW16(C->IoBase, UHCI_USBCMD, (UINT16)(UHCI_CMD_RS | UHCI_CMD_MAXP));

    UhciSurveyCcs(C);
    C->Up = 1;
    LogCtrl(C, Index);
    return 1;
}
