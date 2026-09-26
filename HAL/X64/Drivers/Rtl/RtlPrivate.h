/*
 * RtlPrivate.h — 寄存器与 MMIO（PR-N-rtl-1）
 *
 * 数值对照 Linux drivers/net/ethernet/realtek/r8169_main.c（只读参考）。
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
#define RTL_DID_8136         0x8136u /* FE；仍认，便于常见笔电 */

#define RTL_MAC0             0x00u
#define RTL_BAR_MAP_BYTES    0x1000u

extern volatile UINT8 *gRtlBar;
extern UINT64 gRtlBarPhys;
extern UINT16 gRtlDid;
extern UINT8 gRtlMac[6];
extern int gRtlReady;

static inline UINT8 RtlMmioR8(UINT32 Off) {
    return *(volatile UINT8 *)(gRtlBar + Off);
}

static inline UINT32 RtlMmioR32(UINT32 Off) {
    return *(volatile UINT32 *)(gRtlBar + Off);
}

int RtlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
               UINT16 *DidOut);
int RtlReadMac(UINT8 Mac[6]);

#endif
