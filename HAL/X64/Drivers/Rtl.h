/*
 * Rtl.h — Realtek r8169（RTL8168/8111…）对外 API（PR-N-rtl-1）
 *
 * 本刀：Probe + 读 MAC；不收发、不 NetAttachNic（→ rtl-2）。
 */
#ifndef RTL_H
#define RTL_H

#include "BootTypes.h"

int RtlSetup(void);
int RtlReady(void);
void RtlGetMac(UINT8 Mac[6]);
UINT16 RtlPciDid(void);

#endif
