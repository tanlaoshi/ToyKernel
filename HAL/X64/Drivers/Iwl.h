/*
 * Iwl.h — Intel 8265/8275 对外 API（PR-N-wifi-1）
 *
 * Probe / 固件探测；wifi-2 再挂 Net。
 */
#ifndef IWL_H
#define IWL_H

#include "BootTypes.h"

#define IWL_VENDOR     0x8086u
#define IWL_DID_8265   0x24FDu

int IwlSetup(void);
int IwlReady(void);
void IwlGetMac(UINT8 Mac[6]);
UINT16 IwlPciDid(void);
int IwlFwLoaded(void);

#endif
