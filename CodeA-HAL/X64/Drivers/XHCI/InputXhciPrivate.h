/*
 * InputXhciPrivate.h — InputXhci / InputXhciProbe 内部交接（PR-S3-inputxhci-1）
 */
#ifndef INPUT_XHCI_PRIVATE_H
#define INPUT_XHCI_PRIVATE_H

#include "Driver.h"
#include "XHCI.h"

extern USB_CONTROLLER gXhciDev;
extern int gXhciReady;

int TryXhciAt(UINT64 Base, USB_CONTROLLER *Dev);
int XhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate);

#endif
