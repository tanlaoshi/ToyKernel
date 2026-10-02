/*
 * Nvme.c — 最小 NVMe 读写（PR-H5）
 *
 * 同步轮询、Admin + 单 IO 队列、512B LBA、单页 PRP bounce；无中断。
 * QEMU `-device nvme` / 真机 PCIe NVMe；FAT/VFS 不动。
 */

#include "Nvme.h"
#include "NvmePrivate.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

NVME_CTRL gCtrl[NVME_MAX_CTRL];
int gCtrlCount;
static int gReady;

int NvmeReady(void) {
    return gReady;
}

int NvmeSetup(void) {
    int B;
    int D;
    int F;

    if (gReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }

    gCtrlCount = 0;
    for (B = 0; B < 256 && gCtrlCount < NVME_MAX_CTRL; B++) {
        for (D = 0; D < 32 && gCtrlCount < NVME_MAX_CTRL; D++) {
            for (F = 0; F < 8 && gCtrlCount < NVME_MAX_CTRL; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT32 ClassReg;
                UINT32 Class;
                UINT32 Cmd;
                UINT64 Bar;
                NVME_CTRL *C;

                if ((VidDid & 0xFFFF) == 0xFFFF) {
                    continue;
                }
                ClassReg = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x08);
                Class = (ClassReg >> 8) & 0xFFFFFFu;
                if (Class != NVME_PCI_CLASS) {
                    continue;
                }

                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                if (!PciBar0((UINT8)B, (UINT8)D, (UINT8)F, &Bar)) {
                    continue;
                }

                C = &gCtrl[gCtrlCount];
                ZeroMemory(C, sizeof(*C));
                if (!CtrlInit(C, Bar)) {
                    DebugWrite("nvme: ctrl init fail\n");
                    continue;
                }
                DebugWrite("nvme: ctrl ");
                DebugHex32((UINT32)gCtrlCount);
                DebugWrite(" bar=");
                DebugHex32((UINT32)Bar);
                DebugWrite("\n");
                gCtrlCount++;
            }
        }
    }

    if (gCtrlCount == 0) {
        DebugWrite("nvme: no controllers\n");
        return 0;
    }

    gReady = 1;
    ToyLogFs("Boot: NVMe Drives=");
    {
        static const char Hex[] = "0123456789abcdef";
        char Buf[2];
        Buf[0] = Hex[gCtrlCount & 0xf];
        Buf[1] = 0;
        ToyLogFs(Buf);
    }
    ToyLogFs("\n");
    return 1;
}

int NvmeProbe(UINT32 Drive) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return gCtrl[Drive].Ready ? 1 : 0;
}

int NvmeReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return Xfer(&gCtrl[Drive], Lba, Count, Buffer, 0);
}

int NvmeWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return Xfer(&gCtrl[Drive], Lba, Count, (void *)Buffer, 1);
}
