/*
 * RtlPrivate.h — 寄存器 / 描述符（PR-N-rtl-2）
 *
 * 数值对照 Linux drivers/net/ethernet/realtek/r8169*.c（只读参考，不搬代码）。
 */
#ifndef RTL_PRIVATE_H
#define RTL_PRIVATE_H

#include "Rtl.h"

#define RTL_VENDOR           0x10ECu
#define RTL_DID_8168         0x8168u
#define RTL_DID_8161         0x8161u
#define RTL_DID_8162         0x8162u
#define RTL_DID_8167         0x8167u
#define RTL_DID_8169         0x8169u
#define RTL_DID_8136         0x8136u

#define RTL_MAC0             0x00u
#define RTL_MAR0             0x08u
#define RTL_TNPDS_LO         0x20u
#define RTL_TNPDS_HI         0x24u
#define RTL_CHIPCMD          0x37u
#define RTL_TXPOLL           0x38u
#define RTL_INTRMASK         0x3Cu
#define RTL_INTRSTATUS       0x3Eu
#define RTL_TXCONFIG         0x40u
#define RTL_RXCONFIG         0x44u
#define RTL_CFG9346          0x50u
#define RTL_CONFIG1          0x52u
#define RTL_PHYSTATUS        0x6Cu
#define RTL_RXMAXSIZE        0xDAu
#define RTL_CPLUSCMD         0xE0u
#define RTL_RDSAR_LO         0xE4u
#define RTL_RDSAR_HI         0xE8u

#define RTL_CMD_RESET        0x10u
#define RTL_CMD_RX_ENB       0x08u
#define RTL_CMD_TX_ENB       0x04u
#define RTL_TXPOLL_NPQ       0x40u

#define RTL_RX_ACCEPT_ERR    (1u << 5)
#define RTL_RX_ACCEPT_RUNT   (1u << 4)
#define RTL_RX_ACCEPT_BCAST  (1u << 3)
#define RTL_RX_ACCEPT_MCAST  (1u << 2)
#define RTL_RX_ACCEPT_MYPHYS (1u << 1)
#define RTL_RX_ACCEPT_ALLPHYS (1u << 0)

#define RTL_PHY_LINK         0x02u
#define RTL_CFG9346_UNLOCK   0xC0u
#define RTL_CFG9346_LOCK     0x00u

#define RTL_DESC_OWN         (1u << 31)
#define RTL_DESC_EOR         (1u << 30)
#define RTL_DESC_FS          (1u << 29)
#define RTL_DESC_LS          (1u << 28)
#define RTL_DESC_LEN_MASK    0x3FFFu

#define RTL_BAR_MAP_BYTES    0x1000u
#define RTL_RING_COUNT       16u
#define RTL_BUF_SIZE         1536u
#define RTL_ETH_FCS_LEN      4u

#pragma pack(1)
typedef struct {
    UINT32 Opts1;
    UINT32 Opts2;
    UINT64 Addr;
} RTL_DESC;
#pragma pack()

extern volatile UINT8 *gRtlBar;
extern UINT64 gRtlBarPhys;
extern UINT16 gRtlDid;
extern UINT8 gRtlMac[6];
extern int gRtlReady;
extern UINT32 gRtlLinkMbps;
extern int gRtlLinkFull;

static inline UINT8 RtlMmioR8(UINT32 Off) {
    return *(volatile UINT8 *)(gRtlBar + Off);
}

static inline void RtlMmioW8(UINT32 Off, UINT8 Val) {
    *(volatile UINT8 *)(gRtlBar + Off) = Val;
}

static inline UINT16 RtlMmioR16(UINT32 Off) {
    return *(volatile UINT16 *)(gRtlBar + Off);
}

static inline void RtlMmioW16(UINT32 Off, UINT16 Val) {
    *(volatile UINT16 *)(gRtlBar + Off) = Val;
}

static inline UINT32 RtlMmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gRtlBar + Off);
}

static inline void RtlMmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gRtlBar + Off) = Val;
}

static inline void RtlFence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void RtlZero(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static inline void RtlCopy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

int RtlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
               UINT16 *DidOut);
int RtlReadMac(UINT8 Mac[6]);

void RtlStallMs(UINT32 Ms);
int RtlHwReset(void);
void RtlHwSetMac(const UINT8 Mac[6]);
void RtlHwConfigure(UINT64 TxDescPhys, UINT64 RxDescPhys);
void RtlHwStart(void);
int RtlHwLinkUp(UINT32 *MbpsOut, int *FullOut);

int RtlBringUp(void);

#endif
