#ifndef TOY_NETIF_H
#define TOY_NETIF_H

#include "BootTypes.h"

struct netif;

struct netif *ToyNetifGet(void);
int ToyNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw);
void ToyNetifInput(const UINT8 *Frame, UINTN Len);

#endif
