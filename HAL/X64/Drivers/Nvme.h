/*
 * Nvme.h — PCIe NVMe 块设备（PR-H5）
 *
 * PCI class 01.08.02；经 Driver Block 类注册，Common 只见 Block*。
 */
#ifndef NVME_H
#define NVME_H

#include "BootTypes.h"

int NvmeProbe(UINT32 Drive);
int NvmeReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer);
int NvmeWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer);

/* Driver 入口：扫描 PCI NVMe；需 VirtualMemoryEnabled */
int NvmeSetup(void);
int NvmeReady(void);

#endif
