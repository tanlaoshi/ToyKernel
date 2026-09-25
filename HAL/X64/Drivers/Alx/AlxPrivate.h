/*
 * AlxPrivate.h — 寄存器与 MMIO（PR-N-alx-1）
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

#define ALX_STAD0            0x1488u
#define ALX_STAD1            0x148Cu

#define ALX_BAR_MAP_BYTES    0x20000u

extern volatile UINT8 *gAlxBar;
extern UINT64 gAlxBarPhys;
extern UINT16 gAlxDid;
extern UINT8 gAlxMac[6];
extern int gAlxReady;

static inline UINT32 AlxMmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gAlxBar + Off);
}

static inline void AlxMmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gAlxBar + Off) = Val;
}

int AlxPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
               UINT16 *DidOut);
int AlxReadMac(UINT8 Mac[6]);
int AlxLoadPermMac(UINT8 Mac[6]);

#endif
