#ifndef XHCI_H
#define XHCI_H

#include "BootConfig.h"

int XHCIInit(UINT32 BaseAddress);
int XHCIInit64(UINT64 BaseAddress);

#endif