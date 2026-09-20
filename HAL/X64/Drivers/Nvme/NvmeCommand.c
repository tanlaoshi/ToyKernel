/*
 * NvmeCommand.c — Admin/IO 命令与扇区传送（PR-S-nvme-1）
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

static void DbWrite(NVME_CTRL *C, UINT32 Qid, int Completion, UINT16 Val) {
    UINT32 Off = 0x1000u + ((2u * Qid + (Completion ? 1u : 0u)) * (4u << C->Dstrd));
    MmioW32(C->Bar, Off, Val);
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

int AdminCmd(NVME_CTRL *C, NVME_SQE *Cmd) {
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

int Xfer(NVME_CTRL *C, UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
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
