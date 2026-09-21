/*
 * E1000Private.h — 寄存器、描述符、跨文件状态（PR-S-e1000-1）
 */
#ifndef E1000_PRIVATE_H
#define E1000_PRIVATE_H

#include "E1000.h"

#define E1000_VENDOR          0x8086u
#define E1000_DID_82540EM     0x100Eu /* QEMU -device e1000 */
#define E1000_DID_82574L      0x10D3u /* QEMU -device e1000e */
#define E1000_DID_I219_LM     0x156Fu /* NUC I219-LM（PR-N-i219-did） */

#define E1000_REG_CTRL        0x0000u
#define E1000_REG_STATUS      0x0008u
#define E1000_REG_EEC         0x0010u
#define E1000_REG_EERD        0x0014u
#define E1000_REG_ICR         0x00C0u
#define E1000_REG_ITR         0x00C4u
#define E1000_REG_IMS         0x00D0u
#define E1000_REG_IMC         0x00D8u

#define E1000_ICR_LSC         (1u << 2)
#define E1000_ICR_RXDMT0      (1u << 4)
#define E1000_ICR_RXO         (1u << 6)
#define E1000_ICR_RXT0        (1u << 7)
#define E1000_IMS_LSC         E1000_ICR_LSC
#define E1000_IMS_RXDMT0      E1000_ICR_RXDMT0
#define E1000_IMS_RXO         E1000_ICR_RXO
#define E1000_IMS_RXT0        E1000_ICR_RXT0
#define E1000_IMS_RX \
    (E1000_IMS_LSC | E1000_IMS_RXDMT0 | E1000_IMS_RXO | E1000_IMS_RXT0)
#define E1000_REG_RCTL        0x0100u
#define E1000_REG_TCTL        0x0400u
#define E1000_REG_TIPG        0x0410u
#define E1000_REG_RDBAL       0x2800u
#define E1000_REG_RDBAH       0x2804u
#define E1000_REG_RDLEN       0x2808u
#define E1000_REG_RDH         0x2810u
#define E1000_REG_RDT         0x2818u
#define E1000_REG_TDBAL       0x3800u
#define E1000_REG_TDBAH       0x3804u
#define E1000_REG_TDLEN       0x3808u
#define E1000_REG_TDH         0x3810u
#define E1000_REG_TDT         0x3818u
#define E1000_REG_TXDCTL      0x3828u /* PR-N-i219-tx：I219 队列门控 */
#define E1000_REG_TARC0       0x3840u /* PR-N-i219-tx2：TX 仲裁 */
#define E1000_REG_MTA         0x5200u
#define E1000_REG_RAL         0x5400u
#define E1000_REG_RAH         0x5404u
#define E1000_REG_FEXTNVM11   0x5BBCu /* PR-N-i219-tx3：I219 MULR/flush */

/* TXDCTL：PTHRESH/HTHRESH/WTHRESH + GRAN + QUEUE_ENABLE */
#define E1000_TXDCTL_GRAN         (1u << 24)
#define E1000_TXDCTL_QUEUE_ENABLE (1u << 25)
#define E1000_FEXTNVM11_DISABLE_MULR_FIX (1u << 13)

#define E1000_CTRL_SLU        (1u << 6)
#define E1000_CTRL_RST        (1u << 26)

#define E1000_STATUS_FD       (1u << 0)  /* Full Duplex */
#define E1000_STATUS_LU       (1u << 1)
#define E1000_STATUS_SPEED_SHIFT 6
#define E1000_STATUS_SPEED_MASK  (3u << E1000_STATUS_SPEED_SHIFT)

#define E1000_EERD_START      (1u << 0)
#define E1000_EERD_DONE       (1u << 1)
#define E1000_EERD_ADDR_SHIFT 2
#define E1000_EERD_DATA_SHIFT 16
/* 82540 旧式：DONE=bit4，ADDR=bits15:8 */
#define E1000_EERD_DONE_LEGACY (1u << 4)
#define E1000_EERD_ADDR_LEGACY_SHIFT 8

#define E1000_RCTL_EN         (1u << 1)
#define E1000_RCTL_SBP        (1u << 2)
#define E1000_RCTL_UPE        (1u << 3)
#define E1000_RCTL_MPE        (1u << 4)
#define E1000_RCTL_LPE        (1u << 5)
#define E1000_RCTL_LBM_NONE   (0u << 6)
#define E1000_RCTL_BAM        (1u << 15)
#define E1000_RCTL_BSIZE_2048 (0u << 16)
#define E1000_RCTL_SECRC      (1u << 26)

#define E1000_TCTL_EN         (1u << 1)
#define E1000_TCTL_PSP        (1u << 3)
#define E1000_TCTL_CT_SHIFT   4
#define E1000_TCTL_COLD_SHIFT 12

#define E1000_RX_DD           (1u << 0)
#define E1000_RX_EOP          (1u << 1)
#define E1000_TX_DD           (1u << 0)
#define E1000_TX_CMD_EOP      (1u << 0)
#define E1000_TX_CMD_IFCS     (1u << 1)
#define E1000_TX_CMD_RS       (1u << 3)

#define E1000_RING_COUNT      16u
#define E1000_BUF_SIZE        2048u

typedef struct {
    UINT64 Addr;
    UINT16 Length;
    UINT16 Checksum;
    UINT8  Status;
    UINT8  Errors;
    UINT16 Special;
} __attribute__((packed)) E1000_RX_DESC;

typedef struct {
    UINT64 Addr;
    UINT16 Length;
    UINT8  Cso;
    UINT8  Cmd;
    UINT8  Status;
    UINT8  Css;
    UINT16 Special;
} __attribute__((packed)) E1000_TX_DESC;

extern volatile UINT8 *gBar;
extern UINT64 gBarPhys;
extern UINT16 gPciDid;
extern UINT8 gPciBus;
extern UINT8 gPciDev;
extern UINT8 gPciFn;
extern UINT8 gE1000Mac[6];
extern int gE1000UseIrq;
/* PR-N-i219-txdiag：只读计数；Send 路径递增，不改发送语义 */
extern UINT32 gE1000TxOk;
extern UINT32 gE1000TxFail;
extern INT32 gE1000TxLastRc;

static inline UINT32 MmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gBar + Off);
}

static inline void MmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gBar + Off) = Val;
}

static inline void Fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void ZeroMemory(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static inline void CopyMemory(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

int PciFindE1000(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut, UINT16 *DidOut);
void ReadMac(void);
void E1000ApplyI219TxDctl(void); /* PR-N-i219-tx：已试 ❌ */
void E1000ApplyI219Tarc(void);   /* PR-N-i219-tx2：已试 ❌ */
void E1000FlushI219Rings(void);  /* PR-N-i219-tx3：单假说 ring flush */
int TryEnableMsiRx(void);
int WaitLinkUp(void);

#endif
