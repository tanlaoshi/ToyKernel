/*
 * Alx.h — Qualcomm Atheros alx（AR8161…）对外 API（PR-N-alx-1）
 *
 * 本刀：Probe + 读 MAC；不收发、不 NetAttachNic。
 */
#ifndef ALX_H
#define ALX_H

#include "BootTypes.h"

int AlxSetup(void);
int AlxReady(void);
void AlxGetMac(UINT8 Mac[6]);
UINT16 AlxPciDid(void);

#endif
