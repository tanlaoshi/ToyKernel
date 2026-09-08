/*
 * Nvme.c — 最小 NVMe 读写（PR-H5）
 *
 * 同步轮询、Admin + 单 IO 队列、512B LBA、单页 PRP bounce；无中断。
 * QEMU `-device nvme` / 真机 PCIe NVMe；FAT/VFS 不动。
 */
#include "Nvme.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"

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

static NVME_CTRL gCtrl[NVME_MAX_CTRL];
static int gCtrlCount;
static int gReady;

static UINT32 MmioR32(volatile UINT8 *Bar, UINT32 Off) {
    return *(volatile UINT32 *)(Bar + Off);
}

static void MmioW32(volatile UINT8 *Bar, UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(Bar + Off) = Val;
}

static UINT64 MmioR64(volatile UINT8 *Bar, UINT32 Off) {
    return *(volatile UINT64 *)(Bar + Off);
}

static void MmioW64(volatile UINT8 *Bar, UINT32 Off, UINT64 Val) {
    *(volatile UINT64 *)(Bar + Off) = Val;
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

static void DbWrite(NVME_CTRL *C, UINT32 Qid, int Completion, UINT16 Val) {
    UINT32 Off = 0x1000u + ((2u * Qid + (Completion ? 1u : 0u)) * (4u << C->Dstrd));
    MmioW32(C->Bar, Off, Val);
}

static int WaitCsts(NVME_CTRL *C, UINT32 Mask, UINT32 Want, int Loops) {
    while (Loops-- > 0) {
        UINT32 St = MmioR32(C->Bar, NVME_REG_CSTS);
        if ((St & Mask) == Want) {
            return 1;
        }
        if (St & NVME_CSTS_CFS) {
            return 0;
        }
        HalCpuRelax();
    }
    return 0;
}

static UINT16 AllocCid(NVME_CTRL *C) {
    UINT16 Id = C->NextCid++;
    if (C->NextCid == 0) {
        C->NextCid = 1;
    }
    return Id;
}

static int WaitCqe(NVME_CTRL *C, NVME_CQE *Cq, UINT16 *Head, UINT8 *Phase, UINT16 WantCid, int Io) {
    int Spin = (int)C->TimeoutLoops;
    while (Spin-- > 0) {
        volatile NVME_CQE *Entry = &Cq[*Head];
        UINT16 Sp = Entry->StatusPhase;
        if ((Sp & 1u) == (*Phase & 1u)) {
            UINT16 Cid = Entry->Cid;
            UINT16 Status = (UINT16)(Sp >> 1);
            UINT16 Next = (UINT16)((*Head + 1u) % NVME_QSIZE);
            if (Next == 0) {
                *Phase ^= 1u;
            }
            *Head = Next;
            DbWrite(C, Io ? 1u : 0u, 1, *Head);
            if (Cid != WantCid) {
                continue;
            }
            return Status == 0 ? 1 : 0;
        }
        HalCpuRelax();
    }
    return 0;
}

static int AdminCmd(NVME_CTRL *C, NVME_SQE *Cmd) {
    UINT16 Cid = AllocCid(C);
    UINT16 Tail = C->AsqTail;

    Cmd->Cdw0 = (UINT32)(Cmd->Cdw0 & 0xFFu) | ((UINT32)Cid << 16);
    CopyMemory(&C->Asq[Tail], Cmd, sizeof(*Cmd));
    Fence();
    Tail = (UINT16)((Tail + 1u) % NVME_QSIZE);
    C->AsqTail = Tail;
    DbWrite(C, 0, 0, Tail);
    return WaitCqe(C, C->Acq, &C->AcqHead, &C->AcqPhase, Cid, 0);
}

static int IoCmd(NVME_CTRL *C, NVME_SQE *Cmd) {
    UINT16 Cid = AllocCid(C);
    UINT16 Tail = C->IosqTail;

    Cmd->Cdw0 = (UINT32)(Cmd->Cdw0 & 0xFFu) | ((UINT32)Cid << 16);
    CopyMemory(&C->Iosq[Tail], Cmd, sizeof(*Cmd));
    Fence();
    Tail = (UINT16)((Tail + 1u) % NVME_QSIZE);
    C->IosqTail = Tail;
    DbWrite(C, 1, 0, Tail);
    return WaitCqe(C, C->Iocq, &C->IocqHead, &C->IocqPhase, Cid, 1);
}

static int CtrlDisable(NVME_CTRL *C) {
    UINT32 Cc = MmioR32(C->Bar, NVME_REG_CC);
    MmioW32(C->Bar, NVME_REG_CC, Cc & ~NVME_CC_EN);
    return WaitCsts(C, NVME_CSTS_RDY, 0, C->TimeoutLoops);
}

static int CtrlEnable(NVME_CTRL *C) {
    UINT32 Cc = (6u << NVME_CC_IOSQES_SHIFT) | (4u << NVME_CC_IOCQES_SHIFT) | NVME_CC_EN;
    MmioW32(C->Bar, NVME_REG_CC, Cc);
    return WaitCsts(C, NVME_CSTS_RDY, NVME_CSTS_RDY, C->TimeoutLoops);
}

static int IdentifyNs(NVME_CTRL *C) {
    NVME_SQE Cmd;
    UINT8 *Id = C->Ident;
    UINT8 Flbas;
    UINT8 Lbads;

    ZeroMemory(&Cmd, sizeof(Cmd));
    Cmd.Cdw0 = NVME_OPC_IDENTIFY;
    Cmd.Nsid = 1;
    Cmd.Prp1 = (UINT64)(UINTN)Id;
    Cmd.Cdw10 = NVME_CNS_NS;
    ZeroMemory(Id, PAGE_SIZE);
    if (!AdminCmd(C, &Cmd)) {
        return 0;
    }

    /* NSZE == 0 → 无命名空间 */
    if (*(UINT64 *)(UINTN)Id == 0) {
        return 0;
    }
    Flbas = Id[26];
    /* LBAF[i] at 128+4*i：MS(u16) + LBADS(u8) + RP */
    Lbads = Id[128 + 4u * (Flbas & 0xFu) + 2u];
    if (Lbads < 9 || Lbads > 12) {
        Lbads = 9;
    }
    C->NsId = 1;
    C->LbaShift = Lbads;
    return 1;
}

static int CreateIoQueues(NVME_CTRL *C) {
    NVME_SQE Cmd;

    ZeroMemory(&Cmd, sizeof(Cmd));
    Cmd.Cdw0 = NVME_OPC_CREATE_CQ;
    Cmd.Prp1 = (UINT64)(UINTN)C->Iocq;
    Cmd.Cdw10 = ((NVME_QSIZE - 1u) << 16) | 1u; /* QSIZE | QID=1 */
    Cmd.Cdw11 = 1u; /* PC=1, IEN=0 */
    if (!AdminCmd(C, &Cmd)) {
        return 0;
    }

    ZeroMemory(&Cmd, sizeof(Cmd));
    Cmd.Cdw0 = NVME_OPC_CREATE_SQ;
    Cmd.Prp1 = (UINT64)(UINTN)C->Iosq;
    Cmd.Cdw10 = ((NVME_QSIZE - 1u) << 16) | 1u;
    Cmd.Cdw11 = (1u << 16) | 1u; /* CQID=1 | PC=1 */
    if (!AdminCmd(C, &Cmd)) {
        return 0;
    }
    return 1;
}

static int CtrlInit(NVME_CTRL *C, UINT64 Bar) {
    UINT64 Cap;
    UINT32 To;
    UINT8 *Mem;
    UINT64 Phys;
    UINT32 Aqa;

    if (VirtualMemoryMapRange(Bar, Bar, 0x4000, PTE_PRESENT | PTE_WRITABLE) != 0) {
        DebugWrite("nvme: map BAR failed\n");
        return 0;
    }

    C->BarPhys = Bar;
    C->Bar = (volatile UINT8 *)(UINTN)Bar;
    Cap = MmioR64(C->Bar, NVME_REG_CAP);
    C->Dstrd = (UINT32)((Cap >> 32) & 0xFu);
    To = (UINT32)((Cap >> 24) & 0xFFu);
    if (To == 0) {
        To = 1;
    }
    /* CAP.TO 单位 500ms；用松弛忙等圈数 */
    C->TimeoutLoops = (int)(To * 500000u);
    if (C->TimeoutLoops < 1000000) {
        C->TimeoutLoops = 2000000;
    }

    /* 6 页：ASQ ACQ IOSQ IOCQ Ident Bounce */
    Mem = (UINT8 *)PhysicalMemoryAllocatePages(6);
    if (!Mem) {
        return 0;
    }
    ZeroMemory(Mem, 6u * PAGE_SIZE);
    Phys = (UINT64)(UINTN)Mem;

    C->Asq = (NVME_SQE *)(UINTN)(Mem + 0u * PAGE_SIZE);
    C->Acq = (NVME_CQE *)(UINTN)(Mem + 1u * PAGE_SIZE);
    C->Iosq = (NVME_SQE *)(UINTN)(Mem + 2u * PAGE_SIZE);
    C->Iocq = (NVME_CQE *)(UINTN)(Mem + 3u * PAGE_SIZE);
    C->Ident = Mem + 4u * PAGE_SIZE;
    C->Bounce = Mem + 5u * PAGE_SIZE;
    (void)Phys;

    C->AsqTail = 0;
    C->AcqHead = 0;
    C->IosqTail = 0;
    C->IocqHead = 0;
    C->AcqPhase = 1;
    C->IocqPhase = 1;
    C->NextCid = 1;

    if (!CtrlDisable(C)) {
        DebugWrite("nvme: disable timeout\n");
        return 0;
    }

    Aqa = ((NVME_QSIZE - 1u) << 16) | (NVME_QSIZE - 1u);
    MmioW32(C->Bar, NVME_REG_AQA, Aqa);
    MmioW64(C->Bar, NVME_REG_ASQ, (UINT64)(UINTN)C->Asq);
    MmioW64(C->Bar, NVME_REG_ACQ, (UINT64)(UINTN)C->Acq);

    if (!CtrlEnable(C)) {
        DebugWrite("nvme: enable timeout\n");
        return 0;
    }

    if (!IdentifyNs(C)) {
        DebugWrite("nvme: identify ns fail\n");
        return 0;
    }
    if (!CreateIoQueues(C)) {
        DebugWrite("nvme: create ioq fail\n");
        return 0;
    }

    C->Ready = 1;
    return 1;
}

static int PciBar0(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 *BarOut) {
    UINT32 Lo = PciReadConfig(Bus, Dev, Fn, 0x10);
    UINT32 Hi;
    UINT64 Bar;

    if (Lo & 1u) {
        return 0; /* I/O */
    }
    Bar = Lo & 0xFFFFFFF0ULL;
    if (((Lo >> 1) & 3u) == 2u) {
        Hi = PciReadConfig(Bus, Dev, Fn, 0x14);
        Bar |= ((UINT64)Hi) << 32;
    }
    if (Bar == 0) {
        return 0;
    }
    *BarOut = Bar;
    return 1;
}

static int Xfer(NVME_CTRL *C, UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 Done = 0;
    UINT64 BouncePhys;
    UINT32 SecShift;
    UINT32 SecBytes;

    if (!C || !C->Ready || !Buffer || Count == 0) {
        return 0;
    }

    /* Block 层始终 512B；若 NS 为 4KiB LBA 则拒（课堂 QEMU 用 512） */
    if (C->LbaShift != 9) {
        DebugWrite("nvme: only 512B LBA supported\n");
        return 0;
    }
    SecShift = C->LbaShift;
    SecBytes = 1u << SecShift;
    BouncePhys = (UINT64)(UINTN)C->Bounce;

    while (Done < Count) {
        NVME_SQE Cmd;
        UINT32 Chunk = Count - Done;
        UINT32 Bytes;

        if (Chunk > NVME_BOUNCE_SECTORS) {
            Chunk = NVME_BOUNCE_SECTORS;
        }
        Bytes = Chunk * SecBytes;

        if (Write) {
            CopyMemory(C->Bounce, Data + (UINTN)Done * SecBytes, Bytes);
        }

        ZeroMemory(&Cmd, sizeof(Cmd));
        Cmd.Cdw0 = Write ? NVME_OPC_WRITE : NVME_OPC_READ;
        Cmd.Nsid = C->NsId;
        Cmd.Prp1 = BouncePhys;
        Cmd.Cdw10 = Lba + Done; /* SLBA low */
        Cmd.Cdw11 = 0;
        Cmd.Cdw12 = Chunk - 1u; /* NLB 0-based */

        if (!IoCmd(C, &Cmd)) {
            return 0;
        }
        if (!Write) {
            CopyMemory(Data + (UINTN)Done * SecBytes, C->Bounce, Bytes);
        }
        Done += Chunk;
        (void)SecShift;
    }
    return 1;
}

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
    HalSerialWrite("boot: nvme drives=");
    {
        static const char Hex[] = "0123456789abcdef";
        char Buf[2];
        Buf[0] = Hex[gCtrlCount & 0xf];
        Buf[1] = 0;
        HalSerialWrite(Buf);
    }
    HalSerialWrite("\n");
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
