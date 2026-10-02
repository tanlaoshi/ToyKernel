/*
 * Ehci.h — EHCI 对外 API（PR-H-ehci-1/2/3）
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

/* PR-H-ehci-3：BOT MSC（UsbMsc 门面分支） */
int EhciMscClaim(void);
int EhciMscReady(void);
int EhciMscScan(void);
int EhciMscCapacity(void);
UINT32 EhciMscBlockCount(void);
UINT32 EhciMscBlockSize(void);
int EhciMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer);
int EhciMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer);
int EhciMscFlush(void);
int EhciMscRelease(void);
int EhciMscPresent(void);

/* PR-H-ehci-4：FT232 tee */
int EhciFtdiClaim(void);
void EhciFtdiInvalidate(void);
int EhciFtdiReady(void);
int EhciFtdiWrite(const char *Text); /* 0=ok；<0 失败 */
void EhciFtdiPollRx(void);
int EhciFtdiDataReady(void);
char EhciFtdiReadChar(void);

/* PR-N-wifi-1：RTL8188EU */
int EhciWifiClaim(void);
int EhciWifiReady(void);
UINT16 EhciWifiPid(void);

#endif
