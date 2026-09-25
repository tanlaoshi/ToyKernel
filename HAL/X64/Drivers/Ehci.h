/*
 * Ehci.h — EHCI 对外 API（PR-H-ehci-1：Probe/CCS）
 *
 * 本刀：认卡、handoff、复位、扫端口 CCS；不传控/HID。
 */
#ifndef EHCI_H
#define EHCI_H

#include "BootTypes.h"

int EhciSetup(void);
int EhciReady(void);
/* 所有已起 EHCI 口的 CCS 位图或（诊断用） */
UINT32 EhciCcsMask(void);
/* Shell / PHOTO：ready= n= ccs= #+（会重扫 PORTSC） */
void EhciDiagFormat(char *Buf, int Max);

#endif
