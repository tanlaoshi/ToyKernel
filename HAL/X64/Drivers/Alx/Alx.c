/*
 * Alx.c — TPD/RFD/RRD 环 + SendFrame/Poll（PR-N-alx-2）
 */
#include "Alx.h"
#include "AlxPrivate.h"
#include "PhysicalMemory.h"
#include "Net.h"
#include "Hal.h"
#include "ToySerialLog.h"

static ALX_TPD *gTpd;
static ALX_RFD *gRfd;
static ALX_RRD *gRrd;
static UINT8 *gRxBufs;
static UINT8 *gTxBuf;
static UINT64 gDescPhys;
static UINT16 gTxWrite;
static UINT16 gRxWrite;
static UINT16 gRxRead;
static UINT16 gRrdRead;

static UINT64 RxBufPhys(UINT16 Idx) {
    return gDescPhys + 2u * PAGE_SIZE + (UINT64)Idx * ALX_BUF_SIZE;
}

static void RefillRx(void) {
    UINT16 Next;
    UINT16 Count = 0;

    Next = (UINT16)((gRxWrite + 1u) % ALX_RING_COUNT);
    while (Next != gRxRead) {
        gRfd[Next].Addr = RxBufPhys(Next);
        gRxWrite = Next;
        Next = (UINT16)((gRxWrite + 1u) % ALX_RING_COUNT);
        Count++;
    }
    if (Count) {
        AlxFence();
        AlxMmioW16(ALX_RFD_PIDX, gRxWrite);
    }
}

static void InitRingPtrs(void) {
    UINT32 Hi = (UINT32)(gDescPhys >> 32);
    UINT32 i;

    gTxWrite = 0;
    gRxWrite = 0;
    gRxRead = 0;
    gRrdRead = 0;

    AlxMmioW32(ALX_TX_BASE_ADDR_HI, Hi);
    AlxMmioW32(ALX_RX_BASE_ADDR_HI, Hi);
    AlxMmioW32(ALX_TPD_PRI0_ADDR_LO, (UINT32)gDescPhys);
    AlxMmioW32(ALX_RFD_ADDR_LO, (UINT32)(gDescPhys + PAGE_SIZE));
    AlxMmioW32(ALX_RRD_ADDR_LO, (UINT32)(gDescPhys + PAGE_SIZE + 256u));
    AlxMmioW32(ALX_TPD_RING_SZ, ALX_RING_COUNT);
    AlxMmioW32(ALX_RFD_RING_SZ, ALX_RING_COUNT);
    AlxMmioW32(ALX_RRD_RING_SZ, ALX_RING_COUNT);
    AlxMmioW32(ALX_RFD_BUF_SZ, ALX_BUF_SIZE);
    AlxMmioW32(ALX_SRAM9, ALX_SRAM_LOAD_PTR);

    for (i = 0; i < ALX_RING_COUNT; i++) {
        gRfd[i].Addr = 0;
    }
    /* 留一空位：填 0..N-2 */
    for (i = 0; i < ALX_RING_COUNT - 1u; i++) {
        gRfd[i].Addr = RxBufPhys((UINT16)i);
        gRxWrite = (UINT16)i;
    }
    AlxFence();
    AlxMmioW16(ALX_RFD_PIDX, gRxWrite);
}

int AlxBringUp(void) {
    UINT8 *Mem;
    UINT32 Pages;
    UINT32 Mbps = 0;
    int Full = 1;

    gAlxRxCtrl = ALX_MAC_CTRL_WOLSPED_SWEN | ALX_MAC_CTRL_MHASH_ALG_HI5B |
                 ALX_MAC_CTRL_BRD_EN | ALX_MAC_CTRL_PCRCE | ALX_MAC_CTRL_CRCE |
                 (7u << ALX_MAC_CTRL_PRMBLEN_SHIFT);
    gAlxLinkMbps = 0;
    gAlxLinkFull = 1;

    AlxDisableAspm();
    AlxClearWol();
    AlxResetPhy();
    if (!AlxResetMac()) {
        ToyLogNet("Boot: Alx Reset Fail\n");
        return 0;
    }
    /* MAC 复位会清 STAD：写回永久址 */
    AlxSetMacAddr(gAlxMac);

    Pages = 1u + 1u + 6u + 1u;
    Mem = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Mem) {
        ToyLogNet("Boot: Alx Ring OOM\n");
        return 0;
    }
    AlxZero(Mem, (UINTN)Pages * PAGE_SIZE);
    gDescPhys = (UINT64)(UINTN)Mem;
    gTpd = (ALX_TPD *)(UINTN)Mem;
    gRfd = (ALX_RFD *)(UINTN)(Mem + PAGE_SIZE);
    gRrd = (ALX_RRD *)(UINTN)(Mem + PAGE_SIZE + 256u);
    gRxBufs = Mem + 2u * PAGE_SIZE;
    gTxBuf = Mem + (Pages - 1u) * PAGE_SIZE;

    InitRingPtrs();
    AlxConfigureBasic();

    if (!AlxWaitLink(&Mbps, &Full)) {
        ToyLogNet("Boot: Alx Link Timeout\n");
        return 0;
    }
    gAlxLinkMbps = Mbps;
    gAlxLinkFull = Full;
    AlxStartMac();
    AlxMmioW32(ALX_ISR, ~ALX_ISR_DIS);
    ToyLogNet("Boot: Alx Link Up\n");
    return 1;
}

int AlxSendFrame(const UINT8 *Frame, UINTN Len) {
    ALX_TPD *D;
    UINTN Wire = Len;
    UINT16 Next;
    int Spin;

    if (!gAlxReady || !Frame || Len < 14 || !gTpd) {
        return -1;
    }
    if (Wire > ALX_BUF_SIZE) {
        return -1;
    }
    if (Wire < 60) {
        Wire = 60;
    }

    Next = (UINT16)((gTxWrite + 1u) % ALX_RING_COUNT);
    Spin = 100000;
    while (Spin-- > 0 && Next == AlxMmioR16(ALX_TPD_PRI0_CIDX)) {
        HalCpuRelax();
    }
    if (Next == AlxMmioR16(ALX_TPD_PRI0_CIDX)) {
        return -2;
    }

    AlxZero(gTxBuf, Wire);
    AlxCopy(gTxBuf, Frame, Len);
    D = &gTpd[gTxWrite];
    AlxZero(D, sizeof(*D));
    D->Len = (UINT16)Wire;
    D->Word1 = (1u << TPD_EOP_SHIFT);
    D->AddrLo = (UINT32)(UINTN)gTxBuf;
    D->AddrHi = (UINT32)((UINT64)(UINTN)gTxBuf >> 32);
    AlxFence();
    gTxWrite = Next;
    AlxMmioW16(ALX_TPD_PRI0_PIDX, gTxWrite);

    Spin = 100000;
    while (Spin-- > 0) {
        if (AlxMmioR16(ALX_TPD_PRI0_CIDX) == gTxWrite) {
            return 0;
        }
        HalCpuRelax();
    }
    return -3;
}

void AlxPoll(void) {
    int Left = (int)ALX_RING_COUNT + 2;

    if (!gAlxReady || !gRrd) {
        return;
    }
    (void)AlxMmioR32(ALX_ISR);

    while (Left-- > 0) {
        UINT32 Word3 = gRrd[gRrdRead].Word3;
        UINT32 Word0;
        UINT16 Si;
        UINT16 Nor;
        UINT16 PktLen;

        if ((Word3 & (1u << RRD_UPDATED_SHIFT)) == 0) {
            break;
        }
        gRrd[gRrdRead].Word3 = Word3 & ~(1u << RRD_UPDATED_SHIFT);
        Word0 = gRrd[gRrdRead].Word0;
        Si = (UINT16)((Word0 >> RRD_SI_SHIFT) & RRD_SI_MASK);
        Nor = (UINT16)((Word0 >> RRD_NOR_SHIFT) & RRD_NOR_MASK);
        PktLen = (UINT16)(Word3 & RRD_PKTLEN_MASK);

        if (Nor == 1 && Si == gRxRead &&
            (Word3 & ((1u << RRD_ERR_RES_SHIFT) | (1u << RRD_ERR_LEN_SHIFT))) == 0 &&
            PktLen > ALX_ETH_FCS_LEN + 14u) {
            UINT8 *Buf = gRxBufs + (UINTN)gRxRead * ALX_BUF_SIZE;
            NetInputFrame(Buf, (UINTN)(PktLen - ALX_ETH_FCS_LEN));
        }

        gRxRead = (UINT16)((gRxRead + 1u) % ALX_RING_COUNT);
        gRrdRead = (UINT16)((gRrdRead + 1u) % ALX_RING_COUNT);
        RefillRx();
    }
}

int AlxGetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut) {
    if (!gAlxReady) {
        return -1;
    }
    if (UpOut) {
        *UpOut = (gAlxLinkMbps != 0) ? 1 : 0;
    }
    if (MbpsOut) {
        *MbpsOut = gAlxLinkMbps;
    }
    if (FullDuplexOut) {
        *FullDuplexOut = gAlxLinkFull;
    }
    return 0;
}
