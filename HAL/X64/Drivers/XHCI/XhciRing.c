/*
 * XhciRing.c — PR-H-xhci-core-split-2：TRB 环 / DCBAA / ResolveFwCmdRing
 *
 * 从 Xhci.c 原样搬家；不改语义。环缓冲全局仍在 Xhci.c（勿迁 BSS）。
 * ProcessEvents* 已在 XhciEvent.c，本文件不搬。
 */
#include "XHCI/XhciInternal.h"

void DcbaaSet(UINT32 Slot, UINT64 Phys) {
    if (!gDcbaaLive || Slot > gDcbaaMaxSlot) {
        return;
    }
    gDcbaaLive[Slot] = Phys;
    FlushDma(&gDcbaaLive[Slot], sizeof(UINT64));
}

void DcbaaFlush(void) {
    if (!gDcbaaLive) {
        return;
    }
    FlushDma(gDcbaaLive, sizeof(UINT64) * (gDcbaaMaxSlot + 1));
}

void InitRing(XHCI_TRB *Ring, RING_STATE *St, UINT32 Size) {
    if (Size < 2) {
        Size = RING_SIZE;
    }
    ZeroMemory(Ring, sizeof(XHCI_TRB) * Size);
    Ring[Size - 1].Parameter = PointerToPhysical(&Ring[0]);
    Ring[Size - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | TRB_C;
    St->Enq = 0;
    St->Pcs = 1;
    St->Size = Size;
}

void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control) {
    UINT32 i = St->Enq;
    UINT32 Size = St->Size ? St->Size : RING_SIZE;
    Ring[i].Parameter = Param;
    Ring[i].Status = Status;
    Fence();
    Ring[i].Control = Control | (St->Pcs & 1);
    FlushDma(&Ring[i], sizeof(XHCI_TRB));
    i++;
    if (i == Size - 1) {
        Ring[Size - 1].Parameter = PointerToPhysical(&Ring[0]);
        Ring[Size - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | (St->Pcs & 1);
        FlushDma(&Ring[Size - 1], sizeof(XHCI_TRB));
        i = 0;
        St->Pcs ^= 1;
    }
    St->Enq = i;
}

UINT32 TrbType(UINT32 Control) {
    return (Control >> 10) & 0x3F;
}

/*
 * CRCR 是 dequeue，不是环基址。在同页扫 LINK：Parameter→基址，LINK 下标→长度。
 * 成功则沿用固件环（勿 InitRing 从 dequeue 起当基址清掉）。
 */
int ResolveFwCmdRing(UINT64 DeqPhys, UINT32 Rcs,
                            XHCI_TRB **BaseOut, UINT32 *SizeOut,
                            UINT32 *EnqOut, UINT32 *PcsOut) {
    UINT64 Page = DeqPhys & ~0xFFFULL;
    XHCI_TRB *P = (XHCI_TRB *)(UINTN)Page;
    UINT32 MaxTrb = 0x1000u / (UINT32)sizeof(XHCI_TRB);
    UINT32 DeqOff = (UINT32)((DeqPhys - Page) / sizeof(XHCI_TRB));
    UINT32 i;

    for (i = 0; i < MaxTrb; i++) {
        UINT64 LinkTgt;
        UINT32 BaseOff;
        UINT32 Size;

        FlushDma(&P[i], sizeof(XHCI_TRB));
        if (TrbType(P[i].Control) != TRB_LINK) {
            continue;
        }
        LinkTgt = P[i].Parameter & ~0xFULL;
        if (LinkTgt < Page || LinkTgt >= Page + 0x1000) {
            continue;
        }
        BaseOff = (UINT32)((LinkTgt - Page) / sizeof(XHCI_TRB));
        if (BaseOff > i) {
            continue;
        }
        Size = i - BaseOff + 1;
        if (Size < 16 || DeqOff < BaseOff || DeqOff >= i) {
            continue;
        }
        *BaseOut = &P[BaseOff];
        *SizeOut = Size;
        *EnqOut = DeqOff - BaseOff;
        *PcsOut = Rcs & 1u;
        return 0;
    }
    return -1;
}

/* 敲 Doorbell 通知硬件处理环 */
void RingDoorbell(UINT32 Slot, UINT32 Target) {
    Fence();
    WriteMmio32(gDoorbellBase + Slot * 4, Target & 0xFF);
}
