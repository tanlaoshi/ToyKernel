/*
 * Ahci.c — 最小 AHCI 读写（PR-H1）
 *
 * 同步轮询、单命令槽、DMA 经 bounce；无中断。课堂 QEMU ich9-ahci /
 * 真机 SATA AHCI 均可；遗留 IDE 仍走 Ata。
 */

#include "Ahci.h"
#include "AhciPrivate.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

volatile AHCI_GHC *gHba;
UINT64 gHbaPhys;
AHCI_DRIVE gDrives[AHCI_MAX_DRIVES];
int gDriveCount;
static int gReady;

int AhciReady(void) {
    return gReady;
}

int AhciSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT32 Pi;
    UINT32 Cap;
    int Port;
    int Nslots;

    if (gReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (!PciFindAhci(&Bus, &Dev, &Fn, &Bar)) {
        return 0;
    }

    /* HBA MMIO：至少覆盖 GHC + 32 ports × 0x80 */
    if (VirtualMemoryMapRange(Bar, Bar, 0x1100, PTE_PRESENT | PTE_WRITABLE) != 0) {
        DebugWrite("ahci: map HBA failed\n");
        return 0;
    }

    gHbaPhys = Bar;
    gHba = (volatile AHCI_GHC *)(UINTN)Bar;

    /* 可选 HBA 复位；失败则仍试 AE */
    MmioWrite32(&gHba->Ghc, AHCI_GHC_HR);
    (void)WaitClear(&gHba->Ghc, AHCI_GHC_HR, 1000000);
    MmioWrite32(&gHba->Ghc, AHCI_GHC_AE);

    Cap = MmioRead32(&gHba->Cap);
    Pi = MmioRead32(&gHba->Pi);
    Nslots = (int)((Cap >> 8) & 0x1F) + 1;
    (void)Nslots;

    gDriveCount = 0;
    for (Port = 0; Port < AHCI_MAX_PORTS && gDriveCount < AHCI_MAX_DRIVES; Port++) {
        volatile AHCI_PORT *P;
        UINT32 Ssts;
        UINT32 Sig;

        if (((Pi >> Port) & 1u) == 0) {
            continue;
        }
        P = (volatile AHCI_PORT *)(UINTN)(Bar + 0x100u + (UINT64)Port * 0x80u);
        Ssts = MmioRead32(&P->Ssts);
        if ((Ssts & AHCI_SSTS_DET_MASK) != AHCI_SSTS_DET_PRESENT) {
            continue;
        }
        Sig = MmioRead32(&P->Sig);
        if (Sig != AHCI_SIG_ATA && Sig != 0xFFFFFFFFu) {
            /* 跳过 ATAPI 等；部分仿真器 SIG 未填则仍试 */
            if ((Sig & 0xFFFF) == 0xEB14) {
                continue;
            }
        }
        if (!PortInit(P, &gDrives[gDriveCount])) {
            DebugWrite("ahci: port init fail\n");
            continue;
        }
        DebugWrite("ahci: port ");
        DebugHex32((UINT32)Port);
        DebugWrite(" -> drive ");
        DebugHex32((UINT32)gDriveCount);
        DebugWrite("\n");
        gDriveCount++;
    }

    if (gDriveCount == 0) {
        DebugWrite("ahci: no ATA ports\n");
        return 0;
    }

    gReady = 1;
    ToyLogFs("Boot: AHCI Drives=");
    {
        static const char Hex[] = "0123456789abcdef";
        char B[2];
        B[0] = Hex[gDriveCount & 0xf];
        B[1] = 0;
        ToyLogFs(B);
    }
    ToyLogFs("\n");
    DebugWrite("ahci: hba=");
    DebugHex32((UINT32)gHbaPhys);
    DebugWrite("\n");
    return 1;
}

int AhciProbe(UINT32 Drive) {
    if (!gReady || Drive >= (UINT32)gDriveCount) {
        return 0;
    }
    return gDrives[Drive].Ready ? 1 : 0;
}

int AhciReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    if (!gReady || Drive >= (UINT32)gDriveCount) {
        return 0;
    }
    return PortXfer(&gDrives[Drive], Lba, Count, Buffer, 0);
}

int AhciWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (!gReady || Drive >= (UINT32)gDriveCount) {
        return 0;
    }
    return PortXfer(&gDrives[Drive], Lba, Count, (void *)Buffer, 1);
}
