/*
 * Uhci.h — UHCI 对外 API（PR-H-uhci-1）
 */
#ifndef UHCI_H
#define UHCI_H

#include "BootTypes.h"

int UhciSetup(void);
int UhciReady(void);
UINT32 UhciCcsMask(void);
void UhciDiagFormat(char *Buf, int Max);

#endif
