/*
 * AhciPrivate.h — 寄存器、端口与驱动态（PR-S-ahci-1）
 */
#ifndef AHCI_PRIVATE_H
#define AHCI_PRIVATE_H

#include "Ahci.h"
#include "Block.h"
#include "Hal.h"

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

extern volatile AHCI_GHC *gHba;
extern UINT64 gHbaPhys;
extern AHCI_DRIVE gDrives[AHCI_MAX_DRIVES];
extern int gDriveCount;

static inline UINT32 MmioRead32(volatile UINT32 *Reg) {
    return *Reg;
}

static inline void MmioWrite32(volatile UINT32 *Reg, UINT32 Val) {
    *Reg = Val;
}

static inline void ZeroMemory(void *Ptr, UINTN Len) {
    UINT8 *B = (UINT8 *)Ptr;
    UINTN i;
    for (i = 0; i < Len; i++) {
        B[i] = 0;
    }
}

static inline void CopyMemory(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    UINTN i;
    for (i = 0; i < Len; i++) {
        D[i] = S[i];
    }
}

static inline void Fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline int WaitClear(volatile UINT32 *Reg, UINT32 Mask, int Timeout) {
    while (Timeout-- > 0) {
        if ((MmioRead32(Reg) & Mask) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int PciFindAhci(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut);
int PortInit(volatile AHCI_PORT *Port, AHCI_DRIVE *Drive);
int PortXfer(AHCI_DRIVE *Drive, UINT32 Lba, UINT32 Count, void *Buffer, int Write);

#endif
