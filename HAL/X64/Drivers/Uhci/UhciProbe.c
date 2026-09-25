/*
 * UhciProbe.c — 扫 PCI UHCI 并起控制器（PR-H-uhci-1）
 */
#include "UhciPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"
#include "Debug.h"

static void ResurveyAll(void) {
    int i;
    UINT32 OrMask = 0;

    for (i = 0; i < gUhciCount; i++) {
        if (!gUhci[i].Up || !gUhci[i].IoBase) {
            continue;
        }
        UhciSurveyCcs(&gUhci[i]);
        OrMask |= gUhci[i].CcsMask;
    }
    gUhciCcsOr = OrMask;
}

int UhciSetup(void) {
    USB_CONTROLLER List[8];
    int Count;
    int i;
    int Ok = 0;
    int FoundPci = 0;
    UINT32 OrMask = 0;
    int RealPc = !HalCpuIsHypervisor();

    if (gUhciReady) {
        return 1;
    }

    Count = PciScanUSBControllers(List, 8);
    gUhciCount = 0;
    for (i = 0; i < Count && gUhciCount < UHCI_MAX_CTRL; i++) {
        if (List[i].Type != UHCI_PROG_IF) {
            continue;
        }
        FoundPci = 1;
        gUhci[gUhciCount].Pci = List[i];
        gUhci[gUhciCount].Up = 0;
        gUhci[gUhciCount].CcsMask = 0;
        gUhci[gUhciCount].IoBase = 0;
        gUhci[gUhciCount].FrameList = 0;
        if (UhciInitOne(&gUhci[gUhciCount], gUhciCount)) {
            OrMask |= gUhci[gUhciCount].CcsMask;
            Ok = 1;
        }
        gUhciCount++;
    }

    if (!FoundPci) {
        if (RealPc) {
            ToyBootMarkUsb("Boot: UHCI none (no ProgIF 00)\n");
        } else {
            DebugWrite("UHCI: none\n");
        }
        return 0;
    }

    gUhciCcsOr = OrMask;
    gUhciReady = 1;
    {
        char Hex[12];
        char Dig[5];
        ToyBootMarkUsb("Boot: UHCI CCS ports=");
        HalSerialFormatHex(Hex, OrMask, 4);
        Dig[0] = Hex[2];
        Dig[1] = Hex[3];
        Dig[2] = Hex[4];
        Dig[3] = Hex[5];
        Dig[4] = 0;
        ToyBootMarkUsb(Dig);
        ToyBootMarkUsb(Ok ? " up=1\n" : " up=0\n");
    }
    return 1;
}

int UhciReady(void) {
    return gUhciReady;
}

UINT32 UhciCcsMask(void) {
    return gUhciCcsOr;
}

void UhciDiagFormat(char *Buf, int Max) {
    int n = 0;
    char Hex[12];
    const char *P;
    int i;

    if (!Buf || Max < 8) {
        return;
    }
    Buf[0] = 0;
    if (!gUhciReady) {
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
    if (n < Max - 1) {
        Buf[n++] = (char)('0' + (gUhciCount % 10));
    }
    P = " ccs=";
    while (*P && n < Max - 1) {
        Buf[n++] = *P++;
    }
    HalSerialFormatHex(Hex, gUhciCcsOr, 4);
    for (i = 0; i < 4 && n < Max - 1; i++) {
        Buf[n++] = Hex[2 + i];
    }
    for (i = 0; i < gUhciCount && n < Max - 10; i++) {
        P = " #";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        Buf[n++] = (char)('0' + (i % 10));
        P = "=";
        while (*P && n < Max - 1) {
            Buf[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gUhci[i].CcsMask, 4);
        {
            int k;
            for (k = 0; k < 4 && n < Max - 1; k++) {
                Buf[n++] = Hex[2 + k];
            }
        }
    }
    Buf[n] = 0;
}
