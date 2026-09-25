/*
 * EhciProbe.c — 扫 PCI EHCI 并起控制器（PR-H-ehci-1）
 */
#include "EhciPrivate.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"
#include "Debug.h"

static void ResurveyAll(void) {
    int i;
    UINT32 OrMask = 0;

    for (i = 0; i < gEhciCount; i++) {
        if (!gEhci[i].Up || !gEhci[i].Op) {
            continue;
        }
        EhciSurveyCcs(&gEhci[i]);
        OrMask |= gEhci[i].CcsMask;
    }
    gEhciCcsOr = OrMask;
}

int EhciSetup(void) {
    USB_CONTROLLER List[8];
    int Count;
    int i;
    int Ok = 0;
    int FoundPci = 0;
    UINT32 OrMask = 0;
    int RealPc = !HalCpuIsHypervisor();

    if (gEhciReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }

    Count = PciScanUSBControllers(List, 8);
    gEhciCount = 0;
    for (i = 0; i < Count && gEhciCount < EHCI_MAX_CTRL; i++) {
        if (List[i].Type != EHCI_PROG_IF) {
            continue;
        }
        FoundPci = 1;
        gEhci[gEhciCount].Pci = List[i];
        gEhci[gEhciCount].Up = 0;
        gEhci[gEhciCount].CcsMask = 0;
        gEhci[gEhciCount].Cap = 0;
        gEhci[gEhciCount].Op = 0;
        if (EhciInitOne(&gEhci[gEhciCount], gEhciCount)) {
            OrMask |= gEhci[gEhciCount].CcsMask;
            Ok = 1;
        }
        gEhciCount++;
    }

    if (!FoundPci) {
        /* QEMU 无 EHCI：静默；真机打里程碑便于 PHOTO */
        if (RealPc) {
            ToyBootMarkUsb("Boot: EHCI none (no ProgIF 20)\n");
        } else {
            DebugWrite("EHCI: none\n");
        }
        return 0;
    }

    /* 有 PCI EHCI 即 Ready（Init 失败也占 lsdev，Shell `ehci` 可查） */
    gEhciCcsOr = OrMask;
    gEhciReady = 1;
    {
        char Hex[12];
        ToyBootMarkUsb("Boot: EHCI CCS ports=");
        HalSerialFormatHex(Hex, OrMask, 4);
        /* 跳过 "0x" */
        {
            char Dig[5];
            Dig[0] = Hex[2];
            Dig[1] = Hex[3];
            Dig[2] = Hex[4];
            Dig[3] = Hex[5];
            Dig[4] = 0;
            ToyBootMarkUsb(Dig);
        }
        ToyBootMarkUsb(Ok ? " up=1\n" : " up=0\n");
    }
    return 1;
}

int EhciReady(void) {
    return gEhciReady;
}

UINT32 EhciCcsMask(void) {
    return gEhciCcsOr;
}

void EhciDiagFormat(char *Buf, int Max) {
    int n = 0;
    char Hex[12];
    const char *P;
    int i;

    if (!Buf || Max < 8) {
        return;
    }
    Buf[0] = 0;
    if (!gEhciReady) {
        P = "ready=0";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        Buf[n] = 0;
        return;
    }

    ResurveyAll();

    P = "ready=1 n=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    /* 十进制一位（控制器数 ≤4）；勿用 FormatHex 的 "0x" 头 */
    if (n < Max - 1) {
        Buf[n++] = (char)('0' + (gEhciCount % 10));
    }
    P = " ccs=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gEhciCcsOr, 4);
    for (i = 0; i < 4 && n < Max - 1; i++) {
        Buf[n++] = Hex[2 + i];
    }
    P = gEhciHidReady ? " hid=1" : " hid=0";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = EhciMscReady() ? " msc=1" : " msc=0";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = EhciFtdiReady() ? " ftdi=1" : " ftdi=0";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = " err=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    P = gEhciLastErr ? gEhciLastErr : "?";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    for (i = 0; i < gEhciCount && n < Max - 8; i++) {
        P = " #";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        Buf[n++] = (char)('0' + (i % 10));
        P = "=";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gEhci[i].CcsMask, 4);
        {
            int j;
            for (j = 0; j < 4 && n < Max - 1; j++) {
                Buf[n++] = Hex[2 + j];
            }
        }
    }
    if (gEhciHubNote[0] && n < Max - 8) {
        P = " note=";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        P = gEhciHubNote;
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
    }
    Buf[n] = 0;
}
