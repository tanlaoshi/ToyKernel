/*
 * RtlHw.c — 复位 / 配环地址 / 开 TX·RX / 链路（PR-N-rtl-2）
 */
#include "RtlPrivate.h"
#include "Hal.h"

void RtlStallMs(UINT32 Ms) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T0;
    UINT64 Need;
    UINT64 Now;

    if (Ms == 0) {
        return;
    }
    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    Need = (UINT64)Ms * 3000000ULL;
    for (;;) {
        __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        if (Now - T0 >= Need) {
            break;
        }
        HalCpuRelax();
    }
}

int RtlHwReset(void) {
    int i;

    RtlMmioW8(RTL_CHIPCMD, RTL_CMD_RESET);
    for (i = 0; i < 1000; i++) {
        if ((RtlMmioR8(RTL_CHIPCMD) & RTL_CMD_RESET) == 0) {
            return 1;
        }
        RtlStallMs(1);
    }
    return 0;
}

void RtlHwSetMac(const UINT8 Mac[6]) {
    int i;

    if (!Mac) {
        return;
    }
    RtlMmioW8(RTL_CFG9346, RTL_CFG9346_UNLOCK);
    for (i = 0; i < 6; i++) {
        RtlMmioW8(RTL_MAC0 + (UINT32)i, Mac[i]);
    }
    RtlMmioW8(RTL_CFG9346, RTL_CFG9346_LOCK);
}

void RtlHwConfigure(UINT64 TxDescPhys, UINT64 RxDescPhys) {
    UINT32 RxCfg;

    RtlMmioW16(RTL_INTRMASK, 0);
    RtlMmioW16(RTL_INTRSTATUS, 0xFFFFu);

    RtlMmioW32(RTL_TNPDS_LO, (UINT32)TxDescPhys);
    RtlMmioW32(RTL_TNPDS_HI, (UINT32)(TxDescPhys >> 32));
    RtlMmioW32(RTL_RDSAR_LO, (UINT32)RxDescPhys);
    RtlMmioW32(RTL_RDSAR_HI, (UINT32)(RxDescPhys >> 32));

    RtlMmioW16(RTL_RXMAXSIZE, (UINT16)RTL_BUF_SIZE);

    /* DMA burst / IFG 取常见安全默认 */
    RtlMmioW32(RTL_TXCONFIG, 0x03000700u);

    RxCfg = RTL_RX_ACCEPT_BCAST | RTL_RX_ACCEPT_MCAST | RTL_RX_ACCEPT_MYPHYS |
            RTL_RX_ACCEPT_RUNT | (7u << 13) | (7u << 8);
    RtlMmioW32(RTL_RXCONFIG, RxCfg);

    RtlMmioW16(RTL_CPLUSCMD, RtlMmioR16(RTL_CPLUSCMD) | 0x0001u); /* RxChkSum */
    RtlHwSetMac(gRtlMac);

    /* 组播过滤全开（课堂简路径） */
    RtlMmioW32(RTL_MAR0, 0xFFFFFFFFu);
    RtlMmioW32(RTL_MAR0 + 4u, 0xFFFFFFFFu);
}

void RtlHwStart(void) {
    RtlMmioW8(RTL_CHIPCMD, RTL_CMD_TX_ENB | RTL_CMD_RX_ENB);
    RtlMmioW16(RTL_INTRSTATUS, 0xFFFFu);
}

int RtlHwLinkUp(UINT32 *MbpsOut, int *FullOut) {
    int i;
    UINT8 St;

    for (i = 0; i < 60; i++) {
        St = RtlMmioR8(RTL_PHYSTATUS);
        if (St & RTL_PHY_LINK) {
            if (MbpsOut) {
                if (St & 0x10u) {
                    *MbpsOut = 1000;
                } else if (St & 0x08u) {
                    *MbpsOut = 100;
                } else if (St & 0x04u) {
                    *MbpsOut = 10;
                } else {
                    *MbpsOut = 100; /* Link 起但速率位不清时保守 */
                }
            }
            if (FullOut) {
                *FullOut = (St & 0x01u) ? 1 : 0; /* PHYStatus.FullDup */
            }
            return 1;
        }
        RtlStallMs(50);
    }
    if (MbpsOut) {
        *MbpsOut = 0;
    }
    if (FullOut) {
        *FullOut = 1;
    }
    return 0;
}
