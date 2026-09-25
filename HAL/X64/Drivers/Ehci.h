/*
 * Ehci.h — EHCI 对外 API（PR-H-ehci-1/2）
 */
#ifndef EHCI_H
#define EHCI_H

#include "BootTypes.h"

int EhciSetup(void);
int EhciReady(void);
UINT32 EhciCcsMask(void);
void EhciDiagFormat(char *Buf, int Max);

/* PR-H-ehci-2 */
int EhciHidBringup(void);
int EhciHidReady(void);
void EhciHidPoll(void);
int EhciHidKeyboardDequeue(UINT8 Out[8]);
int EhciHidMousePresent(void);
int EhciHidMouseDequeue(UINT32 *X, UINT32 *Y, UINT8 *Buttons, INT8 *Wheel);

#endif
