/*
 * Ahci.c — 最小 AHCI 读写（PR-H1）
 *
 * 同步轮询、单命令槽、DMA 经 bounce；无中断。课堂 QEMU ich9-ahci /
 * 真机 SATA AHCI 均可；遗留 IDE 仍走 Ata。
 */
#include "Ahci.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

#define AHCI_PCI_CLASS       0x010601u
#define AHCI_MAX_PORTS       32
#define AHCI_MAX_DRIVES      BLOCK_MAX_DRIVES
#define AHCI_BOUNCE_SECTORS  8u

#define AHCI_GHC_HR          (1u << 0)
#define AHCI_GHC_IE          (1u << 1)
#define AHCI_GHC_AE          (1u << 31)

#define AHCI_PxCMD_ST        (1u << 0)
#define AHCI_PxCMD_FRE       (1u << 4)
#define AHCI_PxCMD_FR        (1u << 14)
#define AHCI_PxCMD_CR        (1u << 15)

#define AHCI_PxIS_TFES       (1u << 30)
#define AHCI_PxTFD_BSY       (1u << 7)
#define AHCI_PxTFD_DRQ       (1u << 3)
#define AHCI_PxTFD_ERR       (1u << 0)

#define AHCI_SSTS_DET_MASK   0x0Fu
#define AHCI_SSTS_DET_PRESENT 3u
#define AHCI_SIG_ATA         0x00000101u

#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35

#define FIS_TYPE_REG_H2D     0x27

typedef struct {
    UINT32 Cap;
    UINT32 Ghc;
    UINT32 Is;
    UINT32 Pi;
    UINT32 Vs;
    UINT32 CccCtl;
    UINT32 CccPorts;
    UINT32 EmLoc;
    UINT32 EmCtl;
    UINT32 Cap2;
    UINT32 Bohc;
} AHCI_GHC;

typedef struct {
    UINT32 Clb;
    UINT32 Clbu;
    UINT32 Fb;
    UINT32 Fbu;
    UINT32 Is;
    UINT32 Ie;
    UINT32 Cmd;
    UINT32 Reserved0;
    UINT32 Tfd;
    UINT32 Sig;
    UINT32 Ssts;
    UINT32 Sctl;
    UINT32 Serr;
    UINT32 Sact;
    UINT32 Ci;
    UINT32 Sntf;
    UINT32 Fbs;
    UINT32 Devslp;
    UINT32 Reserved1[10];
    UINT32 Vendor[4];
} AHCI_PORT;

typedef struct {
    UINT16 Flags;
    UINT16 Prdtl;
    UINT32 Prdbc;
    UINT32 Ctba;
    UINT32 Ctbau;
    UINT32 Reserved[4];
} __attribute__((packed)) AHCI_CMD_HDR;

typedef struct {
    UINT32 Dba;
    UINT32 Dbau;
    UINT32 Reserved;
    UINT32 Dbc; /* bits[21:0]=byte_count-1；bit31=I */
} __attribute__((packed)) AHCI_PRDT;

typedef struct {
    UINT8  Fis[64];
    UINT8  Atapi[16];
    UINT8  Reserved[48];
    AHCI_PRDT Prdt[8];
} __attribute__((packed)) AHCI_CMD_TABLE;

typedef struct {
    volatile AHCI_PORT *Port;
    AHCI_CMD_HDR *Cl;
    UINT8 *Fis;
    AHCI_CMD_TABLE *Ct;
    UINT8 *Bounce;
    int Ready;
} AHCI_DRIVE;

static volatile AHCI_GHC *gHba;
static UINT64 gHbaPhys;
static AHCI_DRIVE gDrives[AHCI_MAX_DRIVES];
static int gDriveCount;
static int gReady;

static UINT32 MmioRead32(volatile UINT32 *Reg) {
    return *Reg;
}

static void MmioWrite32(volatile UINT32 *Reg, UINT32 Val) {
    *Reg = Val;
}

static void ZeroMemory(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static void CopyMemory(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static void Fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static int WaitClear(volatile UINT32 *Reg, UINT32 Mask, int Timeout) {
    while (Timeout-- > 0) {
        if ((MmioRead32(Reg) & Mask) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

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

static int PciFindAhci(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut) {
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

static int PortInit(volatile AHCI_PORT *Port, AHCI_DRIVE *Drive) {
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

static int PortXfer(AHCI_DRIVE *Drive, UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    volatile AHCI_PORT *Port;
    AHCI_CMD_TABLE *Ct;
    UINT8 *Fis;
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 Done = 0;
    UINT64 BouncePhys;

    if (!Drive || !Drive->Ready || !Buffer || Count == 0) {
        return 0;
    }

    Port = Drive->Port;
    Ct = Drive->Ct;
    Fis = Ct->Fis;
    BouncePhys = (UINT64)(UINTN)Drive->Bounce;

    while (Done < Count) {
        UINT32 Chunk = Count - Done;
        UINT32 Bytes;
        UINT32 Cur = Lba + Done;
        UINT32 Tfd;
        UINT32 Is;
        int Spin;

        if (Chunk > AHCI_BOUNCE_SECTORS) {
            Chunk = AHCI_BOUNCE_SECTORS;
        }
        Bytes = Chunk * 512u;

        if (Write) {
            CopyMemory(Drive->Bounce, Data + (UINTN)Done * 512u, Bytes);
        }

        if (!WaitClear(&Port->Tfd, AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ, 1000000)) {
            return 0;
        }

        ZeroMemory(Fis, 64);
        Fis[0] = FIS_TYPE_REG_H2D;
        Fis[1] = 1u << 7; /* C=1 */
        Fis[2] = Write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
        Fis[3] = 0;
        Fis[4] = (UINT8)(Cur & 0xFF);
        Fis[5] = (UINT8)((Cur >> 8) & 0xFF);
        Fis[6] = (UINT8)((Cur >> 16) & 0xFF);
        Fis[7] = 0x40; /* LBA */
        Fis[8] = (UINT8)((Cur >> 24) & 0xFF);
        Fis[9] = 0;
        Fis[10] = 0;
        Fis[11] = 0;
        Fis[12] = (UINT8)(Chunk & 0xFF);
        Fis[13] = (UINT8)((Chunk >> 8) & 0xFF);

        ZeroMemory(&Ct->Prdt[0], sizeof(Ct->Prdt[0]));
        Ct->Prdt[0].Dba = (UINT32)BouncePhys;
        Ct->Prdt[0].Dbau = (UINT32)(BouncePhys >> 32);
        Ct->Prdt[0].Dbc = (Bytes - 1u) | (1u << 31);

        Drive->Cl[0].Flags = (UINT16)(5u | (Write ? (1u << 6) : 0));
        Drive->Cl[0].Prdtl = 1;
        Drive->Cl[0].Prdbc = 0;

        MmioWrite32(&Port->Is, 0xFFFFFFFFu);
        Fence();
        MmioWrite32(&Port->Ci, 1u);

        Spin = 2000000;
        while (Spin-- > 0) {
            UINT32 Ci = MmioRead32(&Port->Ci);
            Is = MmioRead32(&Port->Is);
            if ((Ci & 1u) == 0) {
                break;
            }
            if (Is & AHCI_PxIS_TFES) {
                return 0;
            }
            HalCpuRelax();
        }
        if (Spin <= 0) {
            return 0;
        }

        Is = MmioRead32(&Port->Is);
        Tfd = MmioRead32(&Port->Tfd);
        if ((Is & AHCI_PxIS_TFES) || (Tfd & AHCI_PxTFD_ERR)) {
            return 0;
        }
        MmioWrite32(&Port->Is, 0xFFFFFFFFu);

        if (!Write) {
            CopyMemory(Data + (UINTN)Done * 512u, Drive->Bounce, Bytes);
        }
        Done += Chunk;
    }
    return 1;
}

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
    ToyLogFs("boot: ahci drives=");
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
