/*
 * EhciHid.c — Bringup + 武装中断 IN（PR-H-ehci-2）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

static EHCI_CTRL *gHidCtrl;
int gEhciHidReady;
UINT8 gEhciNextAddr = 2;
UINT8 gEhciIntrDt;

void EhciHidArmIntr(EHCI_CTRL *C) {
    EHCI_QTD *T = &C->Qtds[4];
    UINT32 EpChar;
    UINT32 EpCap;
    UINT16 Mps = C->HidMaxPkt;
    UINT8 Sp = C->DevSpeed;
    UINT32 i;

    if (Mps > 64) {
        Mps = 8;
    }
    if (Sp > EHCI_SPEED_HS) {
        Sp = EHCI_SPEED_HS;
    }
    EpChar = ((UINT32)Mps << 16) | ((UINT32)Sp << 12) | ((UINT32)C->HidEp << 8) |
             (UINT32)C->DevAddr;
    /* DTC=1：DT 以 qTD 为准，由 gEhciIntrDt 维护 */
    EpChar |= (1u << 14);
    if (Sp != EHCI_SPEED_HS && C->HubAddr) {
        EpCap = ((UINT32)C->HubAddr << 16) | ((UINT32)C->HubPort << 23) |
                (0xFCu << 8) | 0x01u;
    } else {
        EpCap = 0xFFu;
    }
    C->IntrQh->EpChar = EpChar;
    C->IntrQh->EpCap = EpCap;
    C->IntrQh->Current = 0;
    C->IntrQh->AltNext = EHCI_LINK_TERMINATE;
    C->IntrQh->Token = 0;
    for (i = 0; i < 5; i++) {
        C->IntrQh->Buf[i] = 0;
        C->IntrQh->BufHi[i] = 0;
    }

    for (i = 0; i < 64 && i < 8; i++) {
        C->ReportBuf[i] = 0;
    }
    T->Next = EHCI_LINK_TERMINATE;
    T->AltNext = EHCI_LINK_TERMINATE;
    T->Token = EHCI_QTD_ACTIVE | (3u << 10) | (EHCI_QTD_PID_IN << 8) |
               ((UINT32)Mps << 16) | (gEhciIntrDt ? EHCI_QTD_DT : 0);
    T->Buf[0] = (UINT32)EhciPtrPhys(C->ReportBuf);
    T->Buf[1] = T->Buf[2] = T->Buf[3] = T->Buf[4] = 0;
    T->BufHi[0] = T->BufHi[1] = T->BufHi[2] = T->BufHi[3] = T->BufHi[4] = 0;
    EhciFlush(T, sizeof(*T));
    EhciFlush(C->ReportBuf, 64);
    C->IntrQh->Next = (UINT32)EhciPtrPhys(T);
    EhciFlush(C->IntrQh, sizeof(*C->IntrQh));
    EhciSchedEnablePeriodic(C);
}

int EhciHidBringup(void) {
    int i;
    int Ok = 0;
    UINT32 OrMask = 0;

    gEhciHidReady = 0;
    gHidCtrl = 0;
    gEhciLastErr = "none";
    gEhciNextAddr = 2;
    gEhciHubNote[0] = 0;
    gEhciIntrDt = 0;
    EhciHidResetQueues();

    /* 插拔/上电晚到：再扫一轮 CCS */
    EhciDelay(300000);
    for (i = 0; i < gEhciCount; i++) {
        if (!gEhci[i].Up) {
            continue;
        }
        EhciSurveyCcs(&gEhci[i]);
        OrMask |= gEhci[i].CcsMask;
        gEhci[i].HidOk = 0;
    }
    gEhciCcsOr = OrMask;
    {
        char Hex[12];
        ToyBootMarkUsb("Boot: EHCI HID CCS=");
        HalSerialFormatHex(Hex, OrMask, 4);
        {
            char Dig[5];
            Dig[0] = Hex[2];
            Dig[1] = Hex[3];
            Dig[2] = Hex[4];
            Dig[3] = Hex[5];
            Dig[4] = 0;
            ToyBootMarkUsb(Dig);
        }
        ToyBootMarkUsb("\n");
    }

    for (i = 0; i < gEhciCount; i++) {
        char Tag[4];
        if (!gEhci[i].Up) {
            continue;
        }
        Tag[0] = '#';
        Tag[1] = (char)('0' + (i % 10));
        Tag[2] = 0;
        ToyBootMarkUsb("Boot: EHCI");
        ToyBootMarkUsb(Tag);
        ToyBootMarkUsb(" enum\n");
        if (!EhciSchedStart(&gEhci[i])) {
            continue;
        }
        if (EhciEnumHid(&gEhci[i])) {
            EhciHidArmIntr(&gEhci[i]);
            if (!gHidCtrl) {
                gHidCtrl = &gEhci[i];
            }
            Ok = 1;
            break;
        }
    }
    gEhciHidReady = Ok;
    if (Ok) {
        ToyBootMarkUsb("Boot: EHCI-HID ready (PHOTO 2s)\n");
    } else {
        ToyBootMarkUsb("Boot: EHCI-HID none — ");
        ToyBootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
        ToyBootMarkUsb("\n");
        if (gEhciHubNote[0]) {
            ToyBootMarkUsb("Boot: EHCI note=");
            ToyBootMarkUsb(gEhciHubNote);
            ToyBootMarkUsb("\n");
        }
        ToyBootMarkUsb("Boot: tip=try other USB3 jack; ehci hid\n");
    }
    EhciDelay(20000000);
    return Ok;
}

int EhciHidReady(void) {
    return gEhciHidReady;
}

