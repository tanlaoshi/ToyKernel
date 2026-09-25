/*
 * EhciHidPoll.c — 报告入队 + dequeue（PR-H-ehci-2）
 */
#include "EhciPrivate.h"
#include "Hal.h"
#include "HalVideo.h"

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

void EhciHidResetQueues(void) {
    gKbdHead = gKbdTail = 0;
    gMHead = gMTail = 0;
    gMouseAbsInit = 0;
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
    gMouseAbsX += (int)Dx * 2;
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
            gEhciIntrDt = 0;
            EhciHidArmIntr(C);
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
        gEhciIntrDt = (UINT8)(gEhciIntrDt ? 0 : 1);
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
        EhciHidArmIntr(C);
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
