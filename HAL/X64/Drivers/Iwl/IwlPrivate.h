/*
 * IwlPrivate.h — wifi-1 内部
 */
#ifndef IWL_PRIVATE_H
#define IWL_PRIVATE_H

#include "Iwl.h"

#define IWL_FW_PATH  "FW/IWL8265.UCODE"

extern int gIwlReady;
extern int gIwlFwOk;
extern UINT16 gIwlDid;
extern UINT8 gIwlBus;
extern UINT8 gIwlDev;
extern UINT8 gIwlFn;
extern UINT8 gIwlMac[6];
extern UINTN gIwlFwSize;

int IwlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT16 *DidOut);
int IwlFwTryLoad(void);
void IwlLogBound(void);

#endif
