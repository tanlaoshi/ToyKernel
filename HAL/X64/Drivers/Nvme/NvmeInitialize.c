/*
 * NvmeInitialize.c — 控制器初始化与 PCI BAR（PR-S-nvme-1）
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

int CtrlInit(NVME_CTRL *C, UINT64 Bar) {
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

int PciBar0(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 *BarOut) {
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
