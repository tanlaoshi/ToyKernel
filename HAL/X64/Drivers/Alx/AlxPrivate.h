/*
 * AlxPrivate.h — 寄存器 / 描述符 / 跨文件状态（PR-N-alx-1/2）
 *
 * 数值对照 Linux drivers/net/ethernet/atheros/alx/reg.h（只读参考，不搬代码）。
 */
#ifndef ALX_PRIVATE_H
#define ALX_PRIVATE_H

#include "Alx.h"

#define ALX_VENDOR           0x1969u
#define ALX_DID_AR8161       0x1091u
#define ALX_DID_AR8162       0x1090u
#define ALX_DID_AR8171       0x10A1u
#define ALX_DID_AR8172       0x10A0u

#define ALX_EFLD             0x0204u
#define ALX_EFLD_F_EXIST     (1u << 10)
#define ALX_EFLD_E_EXIST     (1u << 9)
#define ALX_EFLD_STAT        (1u << 5)
#define ALX_EFLD_START       (1u << 0)

#define ALX_SLD              0x0218u
#define ALX_SLD_STAT         (1u << 12)
#define ALX_SLD_START        (1u << 11)

#define ALX_PMCTRL           0x12F8u
#define ALX_PMCTRL_L0S_EN    (1u << 12)
#define ALX_PMCTRL_L1_EN     (1u << 3)

#define ALX_MASTER           0x1400u
#define ALX_MASTER_OOB_DIS   (1u << 6)
#define ALX_MASTER_DMA_MAC_RST (1u << 0)

#define ALX_PHY_CTRL         0x140Cu
#define ALX_PHY_CTRL_100AB_EN (1u << 17)
#define ALX_PHY_CTRL_POWER_DOWN (1u << 14)
#define ALX_PHY_CTRL_PLL_ON  (1u << 13)
#define ALX_PHY_CTRL_RST_ANALOG (1u << 12)
#define ALX_PHY_CTRL_HIB_PULSE (1u << 11)
#define ALX_PHY_CTRL_HIB_EN  (1u << 10)
#define ALX_PHY_CTRL_IDDQ    (1u << 7)
#define ALX_PHY_CTRL_GATE_25M (1u << 5)
#define ALX_PHY_CTRL_LED_MODE (1u << 2)
#define ALX_PHY_CTRL_DSPRST_OUT (1u << 0)
#define ALX_PHY_CTRL_CLS     (ALX_PHY_CTRL_LED_MODE | ALX_PHY_CTRL_100AB_EN | \
                              ALX_PHY_CTRL_PLL_ON)

#define ALX_LPI_CTRL         0x1440u
#define ALX_LPI_CTRL_EN      (1u << 0)
#define ALX_WOL0             0x14A0u

#define ALX_MAC_STS          0x1410u
#define ALX_MAC_STS_IDLE     0xFu

#define ALX_MDIO             0x1414u
#define ALX_MDIO_BUSY        (1u << 27)
#define ALX_MDIO_CLK_SEL_SHIFT 24
#define ALX_MDIO_CLK_SEL_25MD4 0u
#define ALX_MDIO_CLK_SEL_25MD128 7u
#define ALX_MDIO_START       (1u << 23)
#define ALX_MDIO_SPRES_PRMBL (1u << 22)
#define ALX_MDIO_OP_READ     (1u << 21)
#define ALX_MDIO_REG_SHIFT   16
#define ALX_MDIO_DATA_SHIFT  0

#define ALX_SERDES           0x1424u
#define ALX_SERDES_PHYCLK_SLWDWN (1u << 18)
#define ALX_SERDES_MACCLK_SLWDWN (1u << 17)

#define ALX_MAC_CTRL         0x1480u
#define ALX_MAC_CTRL_WOLSPED_SWEN (1u << 30)
#define ALX_MAC_CTRL_MHASH_ALG_HI5B (1u << 29)
#define ALX_MAC_CTRL_BRD_EN  (1u << 26)
#define ALX_MAC_CTRL_SPEED_MASK 0x3u
#define ALX_MAC_CTRL_SPEED_SHIFT 20
#define ALX_MAC_CTRL_SPEED_10_100 1u
#define ALX_MAC_CTRL_SPEED_1000 2u
#define ALX_MAC_CTRL_PRMBLEN_SHIFT 10
#define ALX_MAC_CTRL_PCRCE   (1u << 7)
#define ALX_MAC_CTRL_CRCE    (1u << 6)
#define ALX_MAC_CTRL_FULLD   (1u << 5)
#define ALX_MAC_CTRL_RX_EN   (1u << 1)
#define ALX_MAC_CTRL_TX_EN   (1u << 0)

#define ALX_STAD0            0x1488u
#define ALX_STAD1            0x148Cu
#define ALX_HASH_TBL0        0x1490u
#define ALX_HASH_TBL1        0x1494u
#define ALX_MTU              0x149Cu

#define ALX_RX_BASE_ADDR_HI  0x1540u
#define ALX_TX_BASE_ADDR_HI  0x1544u
#define ALX_RFD_ADDR_LO      0x1550u
#define ALX_RFD_RING_SZ      0x1560u
#define ALX_RFD_BUF_SZ       0x1564u
#define ALX_RRD_ADDR_LO      0x1568u
#define ALX_RRD_RING_SZ      0x1578u
#define ALX_TPD_PRI0_ADDR_LO 0x1580u
#define ALX_TPD_RING_SZ      0x1584u

#define ALX_TXQ0             0x1590u
#define ALX_TXQ0_TXF_BURST_PREF_SHIFT 16
#define ALX_TXQ0_MODE_ENHANCE (1u << 6)
#define ALX_TXQ0_EN          (1u << 5)
#define ALX_TXQ0_TPD_BURSTPREF_SHIFT 0
#define ALX_TXQ_TXF_BURST_PREF_DEF 0x200u
#define ALX_TXQ_TPD_BURSTPREF_DEF 5u

#define ALX_TXQ1             0x1594u
#define ALX_TXQ1_ERRLGPKT_DROP_EN (1u << 11)

#define ALX_RXQ0             0x15A0u
#define ALX_RXQ0_EN          (1u << 31)
#define ALX_RXQ0_RSS_HASH_EN (1u << 29)
#define ALX_RXQ0_NUM_RFD_PREF_SHIFT 20
#define ALX_RXQ0_NUM_RFD_PREF_DEF 8u
#define ALX_RXQ0_IDT_TBL_SIZE_SHIFT 8
#define ALX_RXQ0_IDT_TBL_SIZE_DEF 0x100u
#define ALX_RXQ0_IPV6_PARSE_EN (1u << 7)

#define ALX_DMA              0x15C0u
#define ALX_DMA_WDLY_CNT_SHIFT 16
#define ALX_DMA_WDLY_CNT_DEF 4u
#define ALX_DMA_RDLY_CNT_SHIFT 11
#define ALX_DMA_RDLY_CNT_DEF 15u
#define ALX_DMA_RREQ_PRI_DATA (1u << 10)
#define ALX_DMA_RORDER_MODE_SHIFT 0
#define ALX_DMA_RORDER_MODE_OUT 4u

#define ALX_SRAM9            0x1534u
#define ALX_SRAM_LOAD_PTR    (1u << 0)

#define ALX_RFD_PIDX         0x15E0u
#define ALX_TPD_PRI0_PIDX    0x15F2u
#define ALX_TPD_PRI0_CIDX    0x15F6u
#define ALX_RFD_CIDX         0x15F8u

#define ALX_ISR              0x1600u
#define ALX_ISR_DIS          (1u << 31)
#define ALX_IMR              0x1604u
#define ALX_MSIX_MASK        0x0090u

#define ALX_CLK_GATE         0x1814u
#define ALX_CLK_GATE_ALL     0x3Fu

#define ALX_MISC             0x19C0u
#define ALX_MISC_INTNLOSC_OPEN (1u << 16)
#define ALX_MISC_ISO_EN      (1u << 12)
#define ALX_MISC3            0x19CCu
#define ALX_MISC3_25M_BY_SW  (1u << 1)
#define ALX_MISC3_25M_NOTO_INTNL (1u << 0)

/* MII */
#define MII_BMCR             0x00u
#define MII_BMSR             0x01u
#define MII_ADVERTISE        0x04u
#define MII_CTRL1000         0x09u
#define BMCR_RESET           0x8000u
#define BMCR_ANENABLE        0x1000u
#define BMCR_ANRESTART       0x0200u
#define BMSR_LSTATUS         0x0004u
#define ADVERTISE_CSMA       0x0001u
#define ADVERTISE_10HALF     0x0020u
#define ADVERTISE_10FULL     0x0040u
#define ADVERTISE_100HALF    0x0080u
#define ADVERTISE_100FULL    0x0100u
#define ADVERTISE_PAUSE      0x0400u
#define ADVERTISE_1000FULL   0x0200u /* CTRL1000 */

#define ALX_MII_GIGA_PSSR    0x11u
#define ALX_GIGA_PSSR_SPD_DPLX_RESOLVED 0x0800u
#define ALX_GIGA_PSSR_DPLX   0x2000u
#define ALX_GIGA_PSSR_SPEED  0xC000u
#define ALX_GIGA_PSSR_10MBS  0x0000u
#define ALX_GIGA_PSSR_100MBS 0x4000u
#define ALX_GIGA_PSSR_1000MBS 0x8000u

#define ALX_BAR_MAP_BYTES    0x20000u
#define ALX_RING_COUNT       16u
#define ALX_BUF_SIZE         1536u
#define ALX_ETH_FCS_LEN      4u

#define TPD_EOP_SHIFT        31
#define RRD_PKTLEN_MASK      0x3FFFu
#define RRD_SI_SHIFT         20
#define RRD_SI_MASK          0xFFFu
#define RRD_NOR_SHIFT        16
#define RRD_NOR_MASK         0xFu
#define RRD_ERR_RES_SHIFT    20
#define RRD_ERR_LEN_SHIFT    30
#define RRD_UPDATED_SHIFT    31

#pragma pack(1)
typedef struct {
    UINT16 Len;
    UINT16 Vlan;
    UINT32 Word1;
    UINT32 AddrLo;
    UINT32 AddrHi;
} ALX_TPD;

typedef struct {
    UINT64 Addr;
} ALX_RFD;

typedef struct {
    UINT32 Word0;
    UINT32 RssHash;
    UINT32 Word2;
    UINT32 Word3;
} ALX_RRD;
#pragma pack()

extern volatile UINT8 *gAlxBar;
extern UINT64 gAlxBarPhys;
extern UINT16 gAlxDid;
extern UINT8 gAlxRev;
extern UINT8 gAlxMac[6];
extern int gAlxReady;
extern UINT32 gAlxRxCtrl;
extern UINT32 gAlxLinkMbps;
extern int gAlxLinkFull;

static inline UINT32 AlxMmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gAlxBar + Off);
}

static inline void AlxMmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gAlxBar + Off) = Val;
}

static inline UINT16 AlxMmioR16(UINT32 Off) {
    return *(volatile UINT16 *)(gAlxBar + Off);
}

static inline void AlxMmioW16(UINT32 Off, UINT16 Val) {
    *(volatile UINT16 *)(gAlxBar + Off) = Val;
}

static inline void AlxFence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void AlxZero(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static inline void AlxCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

int AlxPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
               UINT16 *DidOut, UINT8 *RevOut);
int AlxReadMac(UINT8 Mac[6]);
int AlxLoadPermMac(UINT8 Mac[6]);

int AlxResetMac(void);
void AlxResetPhy(void);
void AlxDisableAspm(void);
void AlxClearWol(void);
void AlxSetMacAddr(const UINT8 Mac[6]);
void AlxConfigureBasic(void);
void AlxStartMac(void);
void AlxStallMs(UINT32 Ms);

int AlxMdioRead(UINT16 Reg, UINT16 *Out);
int AlxMdioWrite(UINT16 Reg, UINT16 Val);
int AlxEnsureAutoneg(void);
int AlxWaitLink(UINT32 *MbpsOut, int *FullOut);

int AlxBringUp(void);

#endif
