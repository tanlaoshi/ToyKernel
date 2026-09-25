/*
 * EhciBulk.c — 异步 Bulk（PR-H-ehci-3）
 */
#include "EhciPrivate.h"
#include "Hal.h"
#include "ToySerialLog.h"

/*
 * PR-H-ehci-3：异步 Bulk（复用 CtrlQH；与周期 HID 并行）。
 * Dt 内外维护 DATA0/1；成功后翻转。
 */
int EhciBulkXfer(EHCI_CTRL *C, UINT8 Addr, UINT8 Ep, UINT16 MaxPkt,
                 UINT8 Speed, UINT8 HubAddr, UINT8 HubPort, int DirIn,
                 void *Buf, UINT32 Len, UINT8 *Dt) {
    EHCI_QTD *Td;
    UINT32 EpChar;
    UINT32 EpCap;
    UINT32 Pid;
    UINT8 EpNum;
    UINT32 i;

    if (!C || !C->Sched || !Buf || Len == 0 || !Dt) {
        gEhciLastErr = "bulk bad arg";
        return -1;
    }
    if (Len > 512) {
        gEhciLastErr = "bulk len";
        return -1;
    }
    if (MaxPkt == 0) {
        MaxPkt = 64;
    }
    if (MaxPkt > 512) {
        MaxPkt = 512;
    }
    EpNum = (UINT8)(Ep & 0x0Fu);
    Pid = DirIn ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
    Td = &C->Qtds[0];

    if (!DirIn) {
        EhciCopyBuf(C->BulkBuf, (const UINT8 *)Buf, Len);
    } else {
        for (i = 0; i < Len; i++) {
            C->BulkBuf[i] = 0;
        }
    }

    if (Speed > EHCI_SPEED_HS) {
        Speed = EHCI_SPEED_HS;
    }
    /* DTC + Addr + Ep + MaxPkt + Speed；Bulk 无 Control Flag */
    EpChar = ((UINT32)MaxPkt << 16) | ((UINT32)Speed << 12) |
             ((UINT32)EpNum << 8) | (1u << 14) | (0xFu << 28) | (UINT32)Addr;
    if (Speed != EHCI_SPEED_HS && HubAddr) {
        EpCap = ((UINT32)HubAddr << 16) | ((UINT32)HubPort << 23) | (1u << 30);
    } else {
        EpCap = 1u << 30; /* Mult=1 */
    }

    EhciPrepQtd(Td, EHCI_LINK_TERMINATE, Pid, Len, *Dt ? 1u : 0u,
            EhciPtrPhys(C->BulkBuf));

    if (!EhciAsyncOff(C)) {
        gEhciLastErr = "bulk ase off";
        return -1;
    }
    C->CtrlQh->EpChar = EpChar;
    C->CtrlQh->EpCap = EpCap;
    C->CtrlQh->Current = 0;
    C->CtrlQh->AltNext = EHCI_LINK_TERMINATE;
    C->CtrlQh->Token = 0;
    for (i = 0; i < 5; i++) {
        C->CtrlQh->Buf[i] = 0;
        C->CtrlQh->BufHi[i] = 0;
    }
    C->CtrlQh->Next = (UINT32)EhciPtrPhys(Td);
    C->CtrlQh->Horiz =
        (UINT32)EhciPtrPhys(C->AsyncHead) | EHCI_LINK_TYPE_QH;
    C->AsyncHead->Horiz =
        (UINT32)EhciPtrPhys(C->CtrlQh) | EHCI_LINK_TYPE_QH;
    C->AsyncHead->Token = EHCI_QTD_HALTED;
    C->AsyncHead->Next = EHCI_LINK_TERMINATE;
    EhciFlush(Td, sizeof(*Td));
    EhciFlush(C->BulkBuf, Len);
    EhciFence();
    if (!EhciAsyncOn(C)) {
        gEhciLastErr = "bulk ase on";
        return -1;
    }
    if (!EhciWaitQtd(C, Td)) {
        return -1;
    }
    if (!EhciAsyncOff(C)) {
        gEhciLastErr = "bulk ase off2";
        return -1;
    }
    C->CtrlQh->Next = EHCI_LINK_TERMINATE;
    C->CtrlQh->Token = 0;
    EhciAsyncOn(C);

    *Dt = (UINT8)(!(*Dt));
    if (DirIn) {
        EhciFlush(C->BulkBuf, Len);
        EhciCopyBuf((UINT8 *)Buf, C->BulkBuf, Len);
    }
    return 0;
}
