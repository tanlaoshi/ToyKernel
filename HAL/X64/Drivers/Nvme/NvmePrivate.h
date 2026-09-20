/*
 * NvmePrivate.h — 寄存器、队列与控制器态（PR-S-nvme-1）
 */
#ifndef NVME_PRIVATE_H
#define NVME_PRIVATE_H

#include "Nvme.h"
#include "Block.h"

#define NVME_PCI_CLASS        0x010802u
#define NVME_MAX_CTRL         BLOCK_MAX_DRIVES
#define NVME_QSIZE            16u
#define NVME_BOUNCE_SECTORS   8u

#define NVME_REG_CAP          0x00u
#define NVME_REG_CC           0x14u
#define NVME_REG_CSTS         0x1Cu
#define NVME_REG_AQA          0x24u
#define NVME_REG_ASQ          0x28u
#define NVME_REG_ACQ          0x30u

#define NVME_CC_EN            (1u << 0)
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20
#define NVME_CSTS_RDY         (1u << 0)
#define NVME_CSTS_CFS         (1u << 1)

#define NVME_OPC_CREATE_SQ    0x01u
#define NVME_OPC_CREATE_CQ    0x05u
#define NVME_OPC_IDENTIFY     0x06u
#define NVME_OPC_WRITE        0x01u
#define NVME_OPC_READ         0x02u

#define NVME_CNS_NS           0x00u

typedef struct {
    UINT32 Cdw0;
    UINT32 Nsid;
    UINT64 Reserved0;
    UINT64 Mptr;
    UINT64 Prp1;
    UINT64 Prp2;
    UINT32 Cdw10;
    UINT32 Cdw11;
    UINT32 Cdw12;
    UINT32 Cdw13;
    UINT32 Cdw14;
    UINT32 Cdw15;
} __attribute__((packed)) NVME_SQE;

typedef struct {
    UINT32 Dword0;
    UINT32 Reserved;
    UINT16 SqHead;
    UINT16 SqId;
    UINT16 Cid;
    UINT16 StatusPhase; /* low bit = phase; status = bits[15:1] */
} __attribute__((packed)) NVME_CQE;

typedef struct {
    volatile UINT8 *Bar;
    UINT64 BarPhys;
    UINT32 Dstrd;
    UINT32 TimeoutLoops;
    NVME_SQE *Asq;
    NVME_CQE *Acq;
    NVME_SQE *Iosq;
    NVME_CQE *Iocq;
    UINT8 *Ident;
    UINT8 *Bounce;
    UINT16 AsqTail;
    UINT16 AcqHead;
    UINT16 IosqTail;
    UINT16 IocqHead;
    UINT8 AcqPhase;
    UINT8 IocqPhase;
    UINT16 NextCid;
    UINT32 NsId;
    UINT32 LbaShift; /* 9=512B, 12=4KiB */
    int Ready;
} NVME_CTRL;

extern NVME_CTRL gCtrl[NVME_MAX_CTRL];
extern int gCtrlCount;

static inline UINT32 MmioR32(volatile UINT8 *Bar, UINT32 Off) {
    return *(volatile UINT32 *)(Bar + Off);
}

static inline void MmioW32(volatile UINT8 *Bar, UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(Bar + Off) = Val;
}

static inline UINT64 MmioR64(volatile UINT8 *Bar, UINT32 Off) {
    return *(volatile UINT64 *)(Bar + Off);
}

static inline void MmioW64(volatile UINT8 *Bar, UINT32 Off, UINT64 Val) {
    *(volatile UINT64 *)(Bar + Off) = Val;
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

int AdminCmd(NVME_CTRL *C, NVME_SQE *Cmd);
int CtrlInit(NVME_CTRL *C, UINT64 Bar);
int PciBar0(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 *BarOut);
int Xfer(NVME_CTRL *C, UINT32 Lba, UINT32 Count, void *Buffer, int Write);

#endif
