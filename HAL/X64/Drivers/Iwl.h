/*
 * Iwl.h — Intel 8265/8275 对外 API（PR-N-wifi-2）
 */
#ifndef IWL_H
#define IWL_H

#include "BootTypes.h"

#define IWL_VENDOR     0x8086u
#define IWL_DID_8265   0x24FDu

int IwlSetup(void);
int IwlReady(void);
int IwlAssociated(void);
void IwlGetMac(UINT8 Mac[6]);
UINT16 IwlPciDid(void);
int IwlFwLoaded(void);
int IwlSendFrame(const UINT8 *Frame, UINTN FrameLen);
void IwlPoll(void);
int IwlGetLink(int *Up, UINT32 *Mbps, int *FullDuplex);
int IwlBgBusy(void);
int IwlBgStep(void);
/* NetIwl：Worker 泵（Step + 成功后 Attach） */
int IwlNetBgBusy(void);
void IwlNetBgPump(void);

#endif
