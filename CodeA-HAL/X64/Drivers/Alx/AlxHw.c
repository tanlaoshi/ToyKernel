/*
 * AlxHw.c — MAC/PHY 复位 / 基本配置 / 开 TX·RX（PR-N-alx-2）
 */
#include "AlxPrivate.h"
#include "Hal.h"

void AlxStallMs(UINT32 Ms) {
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

void AlxDisableAspm(void) {
    UINT32 Pm;

    Pm = AlxMmioR32(ALX_PMCTRL);
    Pm &= ~(ALX_PMCTRL_L0S_EN | ALX_PMCTRL_L1_EN);
    AlxMmioW32(ALX_PMCTRL, Pm);
}

void AlxClearWol(void) {
    AlxMmioW32(ALX_WOL0, 0);
}

void AlxSetMacAddr(const UINT8 Mac[6]) {
    UINT32 Stad0;
    UINT32 Stad1;

    if (!Mac) {
        return;
    }
    Stad0 = ((UINT32)Mac[2] << 24) | ((UINT32)Mac[3] << 16) |
            ((UINT32)Mac[4] << 8) | (UINT32)Mac[5];
    Stad1 = ((UINT32)Mac[0] << 8) | (UINT32)Mac[1];
    AlxMmioW32(ALX_STAD0, Stad0);
    AlxMmioW32(ALX_STAD1, Stad1);
}

/*
 * 最小 PHY 核复位（对照 Linux alx_reset_phy 前半；不做 dbg 补丁矩阵）。
 */
void AlxResetPhy(void) {
    UINT32 Val;
    int i;

    Val = AlxMmioR32(ALX_PHY_CTRL);
    Val &= ~(ALX_PHY_CTRL_DSPRST_OUT | ALX_PHY_CTRL_IDDQ |
             ALX_PHY_CTRL_GATE_25M | ALX_PHY_CTRL_POWER_DOWN |
             ALX_PHY_CTRL_CLS);
    Val |= ALX_PHY_CTRL_RST_ANALOG | ALX_PHY_CTRL_HIB_PULSE |
           ALX_PHY_CTRL_HIB_EN;
    AlxMmioW32(ALX_PHY_CTRL, Val);
    AlxStallMs(1);
    AlxMmioW32(ALX_PHY_CTRL, Val | ALX_PHY_CTRL_DSPRST_OUT);
    for (i = 0; i < 80; i++) {
        AlxStallMs(1);
    }

    /* 关 EEE，减少 AN 怪异 */
    Val = AlxMmioR32(ALX_LPI_CTRL);
    AlxMmioW32(ALX_LPI_CTRL, Val & ~ALX_LPI_CTRL_EN);
}

static int StopMac(void) {
    UINT32 Rxq;
    UINT32 Txq;
    UINT32 Sts;
    int i;

    Rxq = AlxMmioR32(ALX_RXQ0);
    AlxMmioW32(ALX_RXQ0, Rxq & ~ALX_RXQ0_EN);
    Txq = AlxMmioR32(ALX_TXQ0);
    AlxMmioW32(ALX_TXQ0, Txq & ~ALX_TXQ0_EN);

    for (i = 0; i < 4000; i++) {
        HalCpuRelax();
    }

    gAlxRxCtrl &= ~(ALX_MAC_CTRL_RX_EN | ALX_MAC_CTRL_TX_EN);
    AlxMmioW32(ALX_MAC_CTRL, gAlxRxCtrl);

    for (i = 0; i < 50; i++) {
        Sts = AlxMmioR32(ALX_MAC_STS);
        if ((Sts & ALX_MAC_STS_IDLE) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0; /* 仍忙也继续复位 */
}

int AlxResetMac(void) {
    UINT32 Val;
    int i;

    AlxMmioW32(ALX_MSIX_MASK, 0xFFFFFFFFu);
    AlxMmioW32(ALX_IMR, 0);
    AlxMmioW32(ALX_ISR, ALX_ISR_DIS);

    (void)StopMac();
    AlxMmioW32(ALX_RFD_PIDX, 1);

    Val = AlxMmioR32(ALX_MASTER);
    AlxMmioW32(ALX_MASTER, Val | ALX_MASTER_DMA_MAC_RST | ALX_MASTER_OOB_DIS);

    for (i = 0; i < 50; i++) {
        HalCpuRelax();
    }
    for (i = 0; i < 50; i++) {
        if (AlxMmioR32(ALX_RFD_PIDX) == 0) {
            break;
        }
        HalCpuRelax();
    }
    for (; i < 100; i++) {
        Val = AlxMmioR32(ALX_MASTER);
        if ((Val & ALX_MASTER_DMA_MAC_RST) == 0) {
            break;
        }
        HalCpuRelax();
    }
    if (i >= 100) {
        return 0;
    }

    Val = AlxMmioR32(ALX_MISC3);
    AlxMmioW32(ALX_MISC3, (Val & ~ALX_MISC3_25M_BY_SW) | ALX_MISC3_25M_NOTO_INTNL);
    Val = AlxMmioR32(ALX_MISC);
    Val &= ~ALX_MISC_INTNLOSC_OPEN;
    AlxMmioW32(ALX_MISC, Val);

    for (i = 0; i < 2000; i++) {
        HalCpuRelax();
    }

    AlxMmioW32(ALX_MAC_CTRL, gAlxRxCtrl);
    Val = AlxMmioR32(ALX_SERDES);
    AlxMmioW32(ALX_SERDES, Val | ALX_SERDES_MACCLK_SLWDWN | ALX_SERDES_PHYCLK_SLWDWN);
    return 1;
}

void AlxConfigureBasic(void) {
    UINT32 Val;
    UINT32 Mtu = 1514u; /* ETH_HLEN+1500+FCS 量级；jumbo 不做 */

    AlxSetMacAddr(gAlxMac);
    AlxMmioW32(ALX_CLK_GATE, ALX_CLK_GATE_ALL);
    AlxMmioW32(ALX_HASH_TBL0, 0);
    AlxMmioW32(ALX_HASH_TBL1, 0);
    AlxMmioW32(ALX_MTU, Mtu);

    AlxMmioW32(ALX_TXQ1, ((Mtu + 7u) >> 3) | ALX_TXQ1_ERRLGPKT_DROP_EN);
    Val = (ALX_TXQ_TPD_BURSTPREF_DEF << ALX_TXQ0_TPD_BURSTPREF_SHIFT) |
          ALX_TXQ0_MODE_ENHANCE |
          (ALX_TXQ_TXF_BURST_PREF_DEF << ALX_TXQ0_TXF_BURST_PREF_SHIFT);
    AlxMmioW32(ALX_TXQ0, Val);

    Val = (ALX_RXQ0_NUM_RFD_PREF_DEF << ALX_RXQ0_NUM_RFD_PREF_SHIFT) |
          (ALX_RXQ0_IDT_TBL_SIZE_DEF << ALX_RXQ0_IDT_TBL_SIZE_SHIFT) |
          ALX_RXQ0_IPV6_PARSE_EN;
    AlxMmioW32(ALX_RXQ0, Val);

    Val = (ALX_DMA_RORDER_MODE_OUT << ALX_DMA_RORDER_MODE_SHIFT) |
          ALX_DMA_RREQ_PRI_DATA |
          (ALX_DMA_WDLY_CNT_DEF << ALX_DMA_WDLY_CNT_SHIFT) |
          (ALX_DMA_RDLY_CNT_DEF << ALX_DMA_RDLY_CNT_SHIFT);
    AlxMmioW32(ALX_DMA, Val);

    AlxMmioW32(ALX_MAC_CTRL, gAlxRxCtrl);
}

void AlxStartMac(void) {
    UINT32 Mac;
    UINT32 Txq;
    UINT32 Rxq;
    UINT32 Sp;

    Rxq = AlxMmioR32(ALX_RXQ0);
    AlxMmioW32(ALX_RXQ0, Rxq | ALX_RXQ0_EN);
    Txq = AlxMmioR32(ALX_TXQ0);
    AlxMmioW32(ALX_TXQ0, Txq | ALX_TXQ0_EN);

    Mac = gAlxRxCtrl;
    if (gAlxLinkFull) {
        Mac |= ALX_MAC_CTRL_FULLD;
    } else {
        Mac &= ~ALX_MAC_CTRL_FULLD;
    }
    Sp = (gAlxLinkMbps == 1000u) ? ALX_MAC_CTRL_SPEED_1000
                                 : ALX_MAC_CTRL_SPEED_10_100;
    Mac &= ~(ALX_MAC_CTRL_SPEED_MASK << ALX_MAC_CTRL_SPEED_SHIFT);
    Mac |= Sp << ALX_MAC_CTRL_SPEED_SHIFT;
    Mac |= ALX_MAC_CTRL_TX_EN | ALX_MAC_CTRL_RX_EN;
    gAlxRxCtrl = Mac;
    AlxMmioW32(ALX_MAC_CTRL, Mac);
}
