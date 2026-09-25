/*
 * EhciHid.c — 周期中断 IN + 软队列（PR-H-ehci-2）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalSerial.h"

#define EHCI_KBD_Q 8
#define EHCI_MOUSE_Q 8

static UINT8 gKbdQ[EHCI_KBD_Q][8];
static UINT8 gKbdHead;
static UINT8 gKbdTail;
static UINT32 gMx[EHCI_MOUSE_Q];
static UINT32 gMy[EHCI_MOUSE_Q];
static UINT8 gMb[EHCI_MOUSE_Q];
static INT8 gMw[EHCI_MOUSE_Q];
static UINT8 gMHead;
static UINT8 gMTail;
static int gMouseAbsX;
static int gMouseAbsY;
static int gMouseAbsInit;
static EHCI_CTRL *gHidCtrl;
int gEhciHidReady;
UINT8 gEhciNextAddr = 2;

static UINT8 gIntrDt; /* 中断 IN 数据开关；重装时不得清零 */

static void ArmIntr(EHCI_CTRL *C) {
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
    /* DTC=1：DT 以 qTD 为准，由 gIntrDt 维护 */
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
               ((UINT32)Mps << 16) | (gIntrDt ? EHCI_QTD_DT : 0);
    T->Buf[0] = (UINT32)EhciPtrPhys(C->ReportBuf);
    T->Buf[1] = T->Buf[2] = T->Buf[3] = T->Buf[4] = 0;
    T->BufHi[0] = T->BufHi[1] = T->BufHi[2] = T->BufHi[3] = T->BufHi[4] = 0;
    EhciFlush(T, sizeof(*T));
    EhciFlush(C->ReportBuf, 64);
    C->IntrQh->Next = (UINT32)EhciPtrPhys(T);
    EhciFlush(C->IntrQh, sizeof(*C->IntrQh));
    EhciSchedEnablePeriodic(C);
}

static void PushKbd(const UINT8 R[8]) {
    UINT8 N = (UINT8)((gKbdTail + 1) % EHCI_KBD_Q);
    int i;
    if (N == gKbdHead) {
        return;
    }
    for (i = 0; i < 8; i++) {
        gKbdQ[gKbdTail][i] = R[i];
    }
    gKbdTail = N;
}

static void PushMouse(INT8 Dx, INT8 Dy, UINT8 Btn, INT8 Wheel) {
    UINT8 N = (UINT8)((gMTail + 1) % EHCI_MOUSE_Q);
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    int MaxX;
    int MaxY;

    if (N == gMHead) {
        return;
    }
    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = 1024;
    }
    if (Sh == 0) {
        Sh = 768;
    }
    MaxX = (int)(Sw > 0 ? Sw - 1 : 0);
    MaxY = (int)(Sh > 0 ? Sh - 1 : 0);
    if (!gMouseAbsInit) {
        gMouseAbsX = (int)(Sw / 2);
        gMouseAbsY = (int)(Sh / 2);
        gMouseAbsInit = 1;
    }
    gMouseAbsX += (int)Dx * 2; /* 真机步进偏小，略放大 */
    gMouseAbsY += (int)Dy * 2;
    if (gMouseAbsX < 0) {
        gMouseAbsX = 0;
    }
    if (gMouseAbsY < 0) {
        gMouseAbsY = 0;
    }
    if (gMouseAbsX > MaxX) {
        gMouseAbsX = MaxX;
    }
    if (gMouseAbsY > MaxY) {
        gMouseAbsY = MaxY;
    }
    gMx[gMTail] = (UINT32)gMouseAbsX;
    gMy[gMTail] = (UINT32)gMouseAbsY;
    gMb[gMTail] = Btn;
    gMw[gMTail] = Wheel;
    gMTail = N;
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
    gIntrDt = 0;
    gMouseAbsInit = 0;

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
            ArmIntr(&gEhci[i]);
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

void EhciHidPoll(void) {
    int i;

    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];
        EHCI_QTD *T;
        UINT32 Tok;
        UINT16 Mps;
        UINT32 Remain;
        UINT32 Got;

        if (!C->HidOk) {
            continue;
        }
        T = &C->Qtds[4];
        EhciFlush(T, sizeof(*T));
        Tok = T->Token;
        if (Tok & EHCI_QTD_ACTIVE) {
            continue;
        }
        if (Tok & EHCI_QTD_HALTED) {
            gIntrDt = 0;
            ArmIntr(C);
            continue;
        }
        Mps = C->HidMaxPkt;
        if (Mps > 64) {
            Mps = 8;
        }
        Remain = (Tok >> 16) & 0x7FFFu;
        if (Remain > Mps) {
            Remain = Mps;
        }
        Got = (UINT32)Mps - Remain;
        /* 成功 IN：翻转 DT，供下次 qTD */
        gIntrDt = (UINT8)(gIntrDt ? 0 : 1);
        EhciFlush(C->ReportBuf, 64);
        if (Got >= 3) {
            if (C->HidProto == 1) {
                PushKbd(C->ReportBuf);
            } else if (C->HidProto == 2) {
                INT8 Dx = (INT8)C->ReportBuf[1];
                INT8 Dy = (INT8)C->ReportBuf[2];
                UINT8 B = (UINT8)(C->ReportBuf[0] & 0x07u);
                INT8 Wh = 0;
                if (Got >= 4) {
                    Wh = (INT8)C->ReportBuf[3];
                }
                PushMouse(Dx, Dy, B, Wh);
            }
        }
        ArmIntr(C);
    }
}

int EhciHidKeyboardDequeue(UINT8 Out[8]) {
    int i;
    if (gKbdHead == gKbdTail || !Out) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        Out[i] = gKbdQ[gKbdHead][i];
    }
    gKbdHead = (UINT8)((gKbdHead + 1) % EHCI_KBD_Q);
    return 1;
}

int EhciHidMousePresent(void) {
    int i;
    if (!gEhciHidReady) {
        return 0;
    }
    for (i = 0; i < gEhciCount; i++) {
        if (gEhci[i].HidOk && gEhci[i].HidProto == 2) {
            return 1;
        }
    }
    return 0;
}

int EhciHidMouseDequeue(UINT32 *X, UINT32 *Y, UINT8 *Buttons, INT8 *Wheel) {
    if (gMHead == gMTail || !X || !Y || !Buttons || !Wheel) {
        return 0;
    }
    *X = gMx[gMHead];
    *Y = gMy[gMHead];
    *Buttons = gMb[gMHead];
    *Wheel = gMw[gMHead];
    gMHead = (UINT8)((gMHead + 1) % EHCI_MOUSE_Q);
    return 1;
}
