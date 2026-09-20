/*
 * AhciInitialize.c — PCI 查找与端口初始化（PR-S-ahci-1）
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

static int PortStop(volatile AHCI_PORT *Port) {
    UINT32 Cmd = MmioRead32(&Port->Cmd);
    if (Cmd & AHCI_PxCMD_ST) {
        MmioWrite32(&Port->Cmd, Cmd & ~AHCI_PxCMD_ST);
        if (!WaitClear(&Port->Cmd, AHCI_PxCMD_CR, 500000)) {
            return 0;
        }
    }
    Cmd = MmioRead32(&Port->Cmd);
    if (Cmd & AHCI_PxCMD_FRE) {
        MmioWrite32(&Port->Cmd, Cmd & ~AHCI_PxCMD_FRE);
        if (!WaitClear(&Port->Cmd, AHCI_PxCMD_FR, 500000)) {
            return 0;
        }
    }
    return 1;
}

static int PortStart(volatile AHCI_PORT *Port) {
    UINT32 Cmd;
    if (!WaitClear(&Port->Cmd, AHCI_PxCMD_CR, 500000)) {
        return 0;
    }
    Cmd = MmioRead32(&Port->Cmd);
    MmioWrite32(&Port->Cmd, Cmd | AHCI_PxCMD_FRE);
    Cmd = MmioRead32(&Port->Cmd);
    MmioWrite32(&Port->Cmd, Cmd | AHCI_PxCMD_ST);
    return 1;
}

int PciFindAhci(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut) {
    int B;
    int D;
    int F;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT32 ClassReg;
                UINT32 Class;
                UINT32 Bar5Lo;
                UINT64 Bar;
                UINT32 Cmd;

                if ((VidDid & 0xFFFF) == 0xFFFF) {
                    continue;
                }
                ClassReg = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x08);
                Class = (ClassReg >> 8) & 0xFFFFFFu;
                if (Class != AHCI_PCI_CLASS) {
                    continue;
                }

                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                /* ABAR = BAR5（AHCI 规范为 32-bit MMIO） */
                Bar5Lo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x24);
                if (Bar5Lo & 1u) {
                    continue; /* I/O BAR，非 MMIO */
                }
                Bar = Bar5Lo & 0xFFFFFFF0ULL;
                if (Bar == 0) {
                    continue;
                }

                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                *BarOut = Bar;
                return 1;
            }
        }
    }
    return 0;
}

int PortInit(volatile AHCI_PORT *Port, AHCI_DRIVE *Drive) {
    UINT8 *Mem;
    UINT64 Phys;

    if (!PortStop(Port)) {
        return 0;
    }

    /* 3 页：CL(1K)+FIS(256) | CT(≤1K) | bounce(4K，最多 8 扇区) */
    Mem = (UINT8 *)PhysicalMemoryAllocatePages(3);
    if (!Mem) {
        return 0;
    }
    ZeroMemory(Mem, 3u * PAGE_SIZE);
    Phys = (UINT64)(UINTN)Mem;

    Drive->Cl = (AHCI_CMD_HDR *)(UINTN)Mem;
    Drive->Fis = Mem + 1024;
    Drive->Ct = (AHCI_CMD_TABLE *)(UINTN)(Mem + PAGE_SIZE);
    Drive->Bounce = Mem + 2u * PAGE_SIZE;
    Drive->Port = Port;

    MmioWrite32(&Port->Clb, (UINT32)Phys);
    MmioWrite32(&Port->Clbu, (UINT32)(Phys >> 32));
    MmioWrite32(&Port->Fb, (UINT32)(Phys + 1024));
    MmioWrite32(&Port->Fbu, (UINT32)((Phys + 1024) >> 32));
    MmioWrite32(&Port->Serr, 0xFFFFFFFFu);
    MmioWrite32(&Port->Is, 0xFFFFFFFFu);
    MmioWrite32(&Port->Ie, 0);

    {
        UINT64 CtPhys = Phys + PAGE_SIZE;
        Drive->Cl[0].Flags = 5; /* CFL = 5 DWORDs of H2D FIS */
        Drive->Cl[0].Prdtl = 1;
        Drive->Cl[0].Prdbc = 0;
        Drive->Cl[0].Ctba = (UINT32)CtPhys;
        Drive->Cl[0].Ctbau = (UINT32)(CtPhys >> 32);
    }

    if (!PortStart(Port)) {
        return 0;
    }
    Drive->Ready = 1;
    return 1;
}
