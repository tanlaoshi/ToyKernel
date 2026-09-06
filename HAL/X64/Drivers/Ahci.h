/*
 * Ahci.h — AHCI HBA 块设备（PR-H1：第二 Block 后端）
 *
 * PCI class 01.06.01；经 Driver Block 类注册，Common 只见 Block*。
 */
#ifndef AHCI_H
#define AHCI_H

#include "BootTypes.h"

int AhciProbe(UINT32 Drive);
int AhciReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
int AhciWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);

/* Driver 入口：扫描 PCI AHCI 并起端口；需 VirtualMemoryEnabled */
int AhciSetup(void);
int AhciReady(void);

#endif
