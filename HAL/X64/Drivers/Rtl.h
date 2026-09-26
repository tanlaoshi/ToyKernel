/*
 * Rtl.h — Realtek r8169（RTL8168/8111…）对外 API（PR-N-rtl-2）
 *
 * Probe/MAC + TX/RX + NIC_L2；Bind → NetAttachNic。
 */
#ifndef RTL_H
#define RTL_H

#include "BootTypes.h"

int RtlSetup(void);
int RtlReady(void);
void RtlGetMac(UINT8 Mac[6]);
UINT16 RtlPciDid(void);
int RtlSendFrame(const UINT8 *Frame, UINTN Len);
void RtlPoll(void);
int RtlGetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut);

#endif
