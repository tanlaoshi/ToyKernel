/*
 * XhciCore.c — PR-H-xhci-split-8：环/控制器/Init/MSC 与共享全局
 *
 * 由单体 Drivers/XHCI.c 剩余部分迁入；子模块见 Drivers/XHCI/ 下各 .c。
 */
#include "XHCI/XhciInternal.h"
#include "Console.h"

UINT64 gCapabilityBase;
UINT64 gOperationalBase;
UINT64 gDoorbellBase;
UINT64 gRuntimeBase;
UINT32 gCtxSize;
UINT32 gMaxPorts;
int gXhciStarted; /* 已对某 BAR 完成 Start；无 HID 时可 Abandon 再试下一颗 */
/* 真机探针：DMAR/TE 留给写 RS 前那行黄字 */
int gXhciDmar = -2; /* -2未查 -1坏 0无 1有 */
int gXhciTe = -2;   /* -2未做 -1失败 0无DRHD 1本关 2已关 */
UINT32 gPort1;
UINT8  gSpeed;
UINT32 gSlotId;
UINT32 gXferSlot;
UINT32 gIntrDci;
UINT16 gEp0Mps;
UINT8  gKbdIface;
UINT8  gKbdParseScore; /* ParseConfig：3=boot键 2=3/1/0 1=其它HID */
UINT8  gKbdEpAddr; /* 配置描述符 bEndpointAddress，匹配事件用 */
UINT16 gKbdMps;    /* ConfigureIntr 记下的 MPS；composite 重建用 */
UINT8  gKbdEpInterval; /* 已换算进 EP 上下文的 Interval 字段 */
UINT8  gUseGetReport;
UINT8  gKbdPollReport; /* 真机复合键鼠：键中断 IN 常 k=0，改 EP0 GET_REPORT */
UINT8  gKbdReportPrev[8];
volatile UINT32 gGetReportBusy;
UINT8  gGetReportFails;
UINT8  gXferFast; /* GET_REPORT 用短超时，避免拖死鼠标 */
UINT8  gUseIrq;
/* 真机默认 POLL；DUAL 见 XhciTryEnterDual（EnableIrq / Arm） */
XHCI_IRQ_MODE gIrqMode = XHCI_IRQ_MODE_POLL;
/* 键盘 Slot 的路由/TT，Configure Endpoint 必须带回，否则 hub 子设备 cfg 失败 */
UINT32 gKbdRoute;
UINT8  gKbdHubSlot;
UINT8  gKbdTtPort;

USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
volatile UINT32 gKeyboardWriteIndex;
volatile UINT32 gKeyboardReadIndex;

UINT32 gMouseSlotId;
UINT32 gMousePort;
UINT32 gMouseRoute;   /* hub 子设备 Route String；根口设备为 0 */
UINT8  gMouseHubSlot; /* TT：父 hub slot；根口为 0 */
UINT8  gMouseTtPort;
UINT32 gMouseIntrDci;
UINT8  gMouseIface;
UINT8  gMouseIfaceProto; /* bInterfaceProtocol：2=boot 相对；0=tablet 等绝对 */
UINT8  gMouseParseScore; /* ParseConfigMouse 评分：3=boot鼠 2=boot子类 1=其它HID */
UINT8  gMouseAbsolute;   /* 1：报告为绝对坐标（QEMU usb-tablet） */
UINT8  gMouseEpAddr;
UINT8  gMouseReportLen;
UINT8  gMouseXferLen; /* 最近一次中断 IN 实际字节（短包后 < MPS） */
UINT8  gMouseBuf[8] __attribute__((aligned(64)));
/* boot 相对鼠：在驱动内累加成屏坐标；PHOTO→桌面时重置到光标 */
int    gMouseAbsX = 512;
int    gMouseAbsY = 384;
int    gMouseAbsInit;
XHCI_TRB gMouseIntrRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gMouseIntr;
/* PR-H-msc-2：Bulk 静态环（仅 InitRing；不配 EP、不门铃、不扫口） */
XHCI_TRB gBulkInRing[RING_SIZE] __attribute__((aligned(64)));
XHCI_TRB gBulkOutRing[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gBulkIn;
RING_STATE gBulkOut;
int gMscBulkRingsInited;
/* PR-H-msc-3/4：scan 临时 / claim 常驻 slot，独立 EP0，勿 InitRing 键盘 gEp0 */
UINT32 gMscScanSlot;
UINT8  gMscScanDevCtx[2048] __attribute__((aligned(64)));
XHCI_TRB gMscScanEp0Ring[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gMscScanEp0;
UINT32 gMscPort;          /* PR-H-msc-4：claim 时根口（hub 子设备亦记 hub 根口） */
UINT32 gMscRoute;
UINT8  gMscHubSlot;
UINT8  gMscTtPort;
UINT32 gMscProbeHubSlot;
UINT32 gMscBulkInDci;
UINT32 gMscBulkOutDci;
UINT16 gMscBulkInMps;
UINT16 gMscBulkOutMps;
int    gMscClaimed;       /* 1：已 SetConfig + Bulk EP */
UINT32 gMscBlockCount;    /* PR-H-msc-5：READ CAPACITY 后扇区数 */
UINT32 gMscBlockSize;
int    gMscCapacityOk;
volatile UINT32 gBulkDone;
volatile UINT32 gBulkCode;
volatile UINT32 gBulkRemain;
UINT8  gMscCfgBuf[1024] __attribute__((aligned(64))); /* 配置描述符；勿用 256 截断 */
UINT8  gMouseDevCtx[2048] __attribute__((aligned(64)));
volatile UINT32 gMouseIntrDone;
volatile UINT32 gIntrReportReady;
volatile UINT32 gMouseReportReady;
/* poll 诊断：PHOTO/桌面可看完成与推送是否在涨 */
volatile UINT32 gStatIntrEvt;
volatile UINT32 gStatMouseEvt;
volatile UINT32 gStatKbdPush;
volatile UINT32 gStatMousePush;
volatile UINT32 gStatLastCc;
volatile UINT32 gStatDrain;
volatile UINT32 gStatXferAny;   /* 任意 Transfer Event */
volatile UINT32 gStatEvtRing;   /* 事件环弹出次数（含命令完成） */
volatile UINT32 gStatLastSlot;
volatile UINT32 gStatLastEp;
volatile UINT32 gStatUnmatched; /* Transfer 且未匹配键鼠 DCI */
volatile UINT32 gStatIrq;       /* PR-H-xhci-stat：XhciIrq 进入次数 */


USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
volatile UINT32 gMouseWriteIndex;
volatile UINT32 gMouseReadIndex;
SPIN_LOCK gHidQueueLock; /* PR-S-ap：IRQ 入队 vs AP 出队 */

XHCI_TRB gCmdRing[RING_SIZE] __attribute__((aligned(64)));
XHCI_TRB gEp0Ring[RING_SIZE] __attribute__((aligned(64)));
XHCI_TRB gHubEp0Ring[RING_SIZE] __attribute__((aligned(64)));   /* hub 专用：勿与键鼠共环 */
XHCI_TRB gMouseEp0Ring[RING_SIZE] __attribute__((aligned(64))); /* 独立鼠/子设备专用 */
XHCI_TRB gIntrRing[RING_SIZE] __attribute__((aligned(64)));
XHCI_TRB gEvtRing[EVT_SIZE] __attribute__((aligned(64)));
/* 真机可指向固件环（IOMMU 已映射）；QEMU 用上面静态缓冲 */
XHCI_TRB *gCmdRingLive = gCmdRing;
XHCI_TRB *gEvtRingLive = gEvtRing;
UINT32 gEvtRingSize = EVT_SIZE;

RING_STATE gCmd;
RING_STATE gEp0;
RING_STATE gHubEp0;
RING_STATE gMouseEp0;
RING_STATE gIntr;
UINT32 gEvtDeq;
UINT32 gEvtCcs;

UINT64 gDcbaa[DCBAA_SLOTS + 1] __attribute__((aligned(64)));
/*
 * 真机 bRS 2：自建 DCBAA/scratch 后 RS 挂；固件 DCBAAP 可 RS。
 * gDcbaaLive 指向固件表或本地 gDcbaa；槽位写入走 DcbaaSet。
 */
UINT64 *gDcbaaLive;
UINT32 gDcbaaMaxSlot;
int gDcbaaFromFirmware;
/*
 * 真机原则：固件已提供的 DMA 结构（DCBAAP/scratch、CRCR、ERST/事件环）优先沿用；
 * 禁止默认改指到内核 .bss。Halt 前快照；仅快照全空时才在固件 DCBAA 同页内切环。
 */
UINT64 gFwDcbaapSave;
UINT64 gFwCrcrSave;   /* CRCR 指针（已清低 6 位）= 当前 dequeue，非必然环基址 */
UINT32 gFwCrcrRcs;    /* CRCR.RCS，与 dequeue 配对 */
UINT64 gFwErstbaSave;
UINT64 gFwEvtSave;
UINT16 gFwEvtSegSave;
UINT64 gFwErdpSave;   /* 固件 ERDP：勿清环后强行改回基址 */
/*
 * HCSPARAMS2 MaxScratchpadBufs（与 Linux HCS_MAX_SCRATCHPAD 一致）：
 *   bits 25:21 = Hi（高 5 位）
 *   bits 31:27 = Lo（低 5 位）
 *   count = (Hi << 5) | Lo
 * 旧式把 Hi/Lo 对调会少/多配页 → 装环后写 RS 时 DMA 踩错 → 真机硬挂。
 */
UINT64 gScratchPtr[XHCI_SCRATCH_MAX] __attribute__((aligned(64)));
UINT8  gScratchBuf[XHCI_SCRATCH_MAX][4096] __attribute__((aligned(4096)));
UINT8  gDevCtx[2048] __attribute__((aligned(64)));
UINT8  gHubDevCtx[2048] __attribute__((aligned(64))); /* PR-H-hub */
UINT8  gInCtx[2048] __attribute__((aligned(64)));
UINT8  gCtrlBuf[256] __attribute__((aligned(64)));
UINT8  gReportBuf[8] __attribute__((aligned(64)));
UINT8  gErst[16] __attribute__((aligned(64)));

UINT32 gHubSlotId;
UINT32 gHubRootPort;
UINT8  gHubNumPorts;
UINT8  gHubSpeed;
UINT8  gHubMtt; /* bDeviceProtocol==2 才置 MTT；误置单 TT hub 会导致子设备中断永不完成 */
UINT8  gHubTtt; /* Hub Desc wHubCharacteristics[6:5] → Slot TT Think Time */
UINT32 gPortNoHid; /* 键盘 pass 已判非 HID 的根口（如前面 U 盘） */
UINT32 gPortNeedForcePr; /* 本轮已 Address+Disable，再扫须强制 PR */
/* Address 走 gEp0 的 slot：claim 成鼠标后仍须用 gEp0，勿切 gMouseEp0 */
UINT8  gSlotEp0UsesKbdRing[DCBAA_SLOTS + 1];

volatile UINT32 gCmdDone;
UINT32 gCmdCode;
UINT32 gCmdSlot;
volatile UINT32 gXferDone;
UINT32 gXferCode;
UINT32 gXferRemain;
volatile UINT32 gIntrDone;

/* 读 MMIO 32 位 */
UINT32 ReadMmio32(UINT64 Addr) {
    return *(volatile UINT32 *)(UINTN)Addr;
}

/* 写 MMIO 32 位 */
void WriteMmio32(UINT64 Addr, UINT32 Value) {
    *(volatile UINT32 *)(UINTN)Addr = Value;
}

/* 写 MMIO 64 位（分两次 32 位写） */
void WriteMmio64(UINT64 Addr, UINT64 Value) {
    WriteMmio32(Addr, (UINT32)Value);
    WriteMmio32(Addr + 4, (UINT32)(Value >> 32));
}

UINT64 ReadMmio64(UINT64 Addr) {
    UINT64 Lo = ReadMmio32(Addr);
    UINT64 Hi = ReadMmio32(Addr + 4);
    return Lo | (Hi << 32);
}

void FlushDma(const void *Ptr, UINTN Size);

void DcbaaSet(UINT32 Slot, UINT64 Phys) {
    if (!gDcbaaLive || Slot > gDcbaaMaxSlot) {
        return;
    }
    gDcbaaLive[Slot] = Phys;
    FlushDma(&gDcbaaLive[Slot], sizeof(UINT64));
}

void DcbaaFlush(void) {
    if (!gDcbaaLive) {
        return;
    }
    FlushDma(gDcbaaLive, sizeof(UINT64) * (gDcbaaMaxSlot + 1));
}

/* 虚拟地址转物理地址（恒等映射） */
UINT64 PointerToPhysical(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

/* 内存屏障，保证 TRB 写入对硬件可见 */
void Fence(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

/* 把 DMA 缓冲从 CPU cache 推出去（真机 RS 后 DMA 读环/DCBAA） */
void FlushDma(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    if (!Ptr || Size == 0) {
        return;
    }
    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    Fence();
}

/* 清零内存块 */
void ZeroMemory(void *Ptr, UINTN Size) {
    UINT8 *P = (UINT8 *)Ptr;
    while (Size--) {
        *P++ = 0;
    }
}

void CopyMemory(void *Dst, const void *Src, UINTN Size) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    while (Size--) {
        *D++ = *S++;
    }
}


/* 等待寄存器 Mask 位清零 */
int WaitClear(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (!(ReadMmio32(Addr) & Mask)) {
            return 1;
        }
    }
    return 0;
}

/* 等待寄存器 Mask 位置位 */
int WaitSet(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (ReadMmio32(Addr) & Mask) {
            return 1;
        }
    }
    return 0;
}

UINT64 ReadTsc(void) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
}

/* 真机忙等，按 ~3GHz 估算。QEMU 不要用长 Stall。
 * 每 ~1ms Drain 一次事件环，避免 Reset/claim 长 Stall 把 HID 饿死。 */
void StallMs(UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;
    UINT64 NextDrain;

    if (Ms == 0) {
        return;
    }
    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    NextDrain = T0;
    while (ReadTsc() - T0 < Need) {
        if (!HalCpuIsHypervisor() && gXhciStarted && ReadTsc() >= NextDrain) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            NextDrain = ReadTsc() + 3000000ULL;
        }
        __asm__ volatile ("pause");
    }
}

int WaitSetMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;

    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    for (;;) {
        if (ReadMmio32(Addr) & Mask) {
            return 1;
        }
        if (ReadTsc() - T0 >= Need) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
}

int WaitClearMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;

    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    for (;;) {
        if (!(ReadMmio32(Addr) & Mask)) {
            return 1;
        }
        if (ReadTsc() - T0 >= Need) {
            return 0;
        }
        __asm__ volatile ("pause");
    }
}


/* 初始化 TRB 环状态 */
void InitRing(XHCI_TRB *Ring, RING_STATE *St, UINT32 Size) {
    if (Size < 2) {
        Size = RING_SIZE;
    }
    ZeroMemory(Ring, sizeof(XHCI_TRB) * Size);
    Ring[Size - 1].Parameter = PointerToPhysical(&Ring[0]);
    Ring[Size - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | TRB_C;
    St->Enq = 0;
    St->Pcs = 1;
    St->Size = Size;
}

/* 向环尾入队一条 TRB */
void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control) {
    UINT32 i = St->Enq;
    UINT32 Size = St->Size ? St->Size : RING_SIZE;
    Ring[i].Parameter = Param;
    Ring[i].Status = Status;
    Fence();
    Ring[i].Control = Control | (St->Pcs & 1);
    FlushDma(&Ring[i], sizeof(XHCI_TRB));
    i++;
    if (i == Size - 1) {
        Ring[Size - 1].Parameter = PointerToPhysical(&Ring[0]);
        Ring[Size - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | (St->Pcs & 1);
        FlushDma(&Ring[Size - 1], sizeof(XHCI_TRB));
        i = 0;
        St->Pcs ^= 1;
    }
    St->Enq = i;
}

UINT32 TrbType(UINT32 Control) {
    return (Control >> 10) & 0x3F;
}

int MapXhciDma(UINT64 Phys, UINTN Bytes) {
    UINT64 Page = Phys & ~0xFFFULL;
    UINTN Span = (UINTN)((Phys + Bytes + 0xFFFULL) - Page);
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (VirtualMemoryMapRange(Page, Page, Span, PTE_XHCI_DMA) == 0) {
        return 0;
    }
    /*
     * 低位 identity 常为 2MB huge，PageWalk 无法拆 PTE → Map 失败。
     * 仍可经 huge 访问；仅缺 UC。高位无映射则必须失败。
     */
    if (Page + Span <= (512ULL << 20)) {
        return 0;
    }
    return -1;
}

/*
 * CRCR 是 dequeue，不是环基址。在同页扫 LINK：Parameter→基址，LINK 下标→长度。
 * 成功则沿用固件环（勿 InitRing 从 dequeue 起当基址清掉）。
 */
int ResolveFwCmdRing(UINT64 DeqPhys, UINT32 Rcs,
                            XHCI_TRB **BaseOut, UINT32 *SizeOut,
                            UINT32 *EnqOut, UINT32 *PcsOut) {
    UINT64 Page = DeqPhys & ~0xFFFULL;
    XHCI_TRB *P = (XHCI_TRB *)(UINTN)Page;
    UINT32 MaxTrb = 0x1000u / (UINT32)sizeof(XHCI_TRB);
    UINT32 DeqOff = (UINT32)((DeqPhys - Page) / sizeof(XHCI_TRB));
    UINT32 i;

    for (i = 0; i < MaxTrb; i++) {
        UINT64 LinkTgt;
        UINT32 BaseOff;
        UINT32 Size;

        FlushDma(&P[i], sizeof(XHCI_TRB));
        if (TrbType(P[i].Control) != TRB_LINK) {
            continue;
        }
        LinkTgt = P[i].Parameter & ~0xFULL;
        if (LinkTgt < Page || LinkTgt >= Page + 0x1000) {
            continue;
        }
        BaseOff = (UINT32)((LinkTgt - Page) / sizeof(XHCI_TRB));
        if (BaseOff > i) {
            continue;
        }
        Size = i - BaseOff + 1;
        if (Size < 16 || DeqOff < BaseOff || DeqOff >= i) {
            continue;
        }
        *BaseOut = &P[BaseOff];
        *SizeOut = Size;
        *EnqOut = DeqOff - BaseOff;
        *PcsOut = Rcs & 1u;
        return 0;
    }
    return -1;
}

/* 处理事件环中所有待处理 TRB（命令完成、传输完成） */
void ProcessEvents(void) {
    int Progress = 0;
    UINT32 EvtSize = gEvtRingSize ? gEvtRingSize : EVT_SIZE;
    int Guard = 0;

    for (;;) {
        XHCI_TRB *Evt;
        if (++Guard > (int)(EvtSize * 2u + 8u)) {
            break; /* 固件残留事件勿死循环 */
        }
        Evt = &gEvtRingLive[gEvtDeq];
        /* 真机：先 invalidate，再读 Cycle，避免缓存挡住完成事件 */
        FlushDma(Evt, sizeof(*Evt));
        if ((Evt->Control & TRB_C) != gEvtCcs) {
            break;
        }
        Progress = 1;
        gStatEvtRing++;

        UINT32 Type = TrbType(Evt->Control);
        UINT32 Code = (Evt->Status >> 24) & 0xFF;
        UINT32 Slot = (Evt->Control >> 24) & 0xFF;

        if (Type == TRB_CMD_COMPLETION) {
            gCmdCode = Code;
            gCmdSlot = Slot;
            gCmdDone = 1;
        } else if (Type == TRB_TRANSFER_EVENT) {
            UINT32 Ep = (Evt->Control >> 16) & 0x1F;
            UINT32 EvtSlot = (Evt->Control >> 24) & 0xFF;
            UINT64 TrbPtr = Evt->Parameter & ~0xFULL;
            UINT64 KbdLo = PointerToPhysical(gIntrRing);
            UINT64 KbdHi = KbdLo + sizeof(gIntrRing);
            UINT64 MouseLo = PointerToPhysical(gMouseIntrRing);
            UINT64 MouseHi = MouseLo + sizeof(gMouseIntrRing);
            int Matched = 0;
            int KbdHit = 0;
            int MouseHit = 0;

            gStatXferAny++;
            gStatLastCc = Code;
            gStatLastSlot = EvtSlot;
            gStatLastEp = Ep;

            /* EP0(DCI=1) 才唤醒 WaitTransfer，避免 HID IN 误完成 EP0 等待 */
            if (EvtSlot == gXferSlot && Ep == 1) {
                gXferCode = Code;
                gXferRemain = Evt->Status & 0xFFFFFF;
                gXferDone = 1;
                Matched = 1; /* GET_REPORT/控制传输：勿记入 unmatched 刷屏 */
            }
            /* PR-H-msc-5：Bulk 完成（slot+DCI 或 TRB 落在 Bulk 环） */
            if (!Matched && gMscScanSlot != 0 && EvtSlot == gMscScanSlot &&
                ((gMscBulkInDci != 0 && Ep == gMscBulkInDci) ||
                 (gMscBulkOutDci != 0 && Ep == gMscBulkOutDci))) {
                gBulkCode = Code;
                gBulkRemain = Evt->Status & 0xFFFFFF;
                gBulkDone = 1;
                Matched = 1;
            }
            if (!Matched) {
                UINT64 BulkInLo = PointerToPhysical(gBulkInRing);
                UINT64 BulkInHi = BulkInLo + sizeof(gBulkInRing);
                UINT64 BulkOutLo = PointerToPhysical(gBulkOutRing);
                UINT64 BulkOutHi = BulkOutLo + sizeof(gBulkOutRing);

                if ((TrbPtr >= BulkInLo && TrbPtr < BulkInHi) ||
                    (TrbPtr >= BulkOutLo && TrbPtr < BulkOutHi)) {
                    gBulkCode = Code;
                    gBulkRemain = Evt->Status & 0xFFFFFF;
                    gBulkDone = 1;
                    Matched = 1;
                }
            }
            /*
             * 中断 EP：只认 slot+DCI，或完成 TRB 落在中断环内。
             * 勿用 EpNum（易与 EP0 的 EndpointID=1 撞）或报告缓冲指针
             * （GET_REPORT 数据 TRB 也指向报告区 → 假 i=、干扰推送）。
             */
            {
                KbdHit = (gSlotId != 0 && gIntrDci != 0 && Ep != 1 &&
                          ((EvtSlot == gSlotId && Ep == gIntrDci) ||
                           (TrbPtr >= KbdLo && TrbPtr < KbdHi)));
                MouseHit = (gMouseSlotId != 0 && gMouseIntrDci != 0 && Ep != 1 &&
                            ((EvtSlot == gMouseSlotId && Ep == gMouseIntrDci) ||
                             (TrbPtr >= MouseLo && TrbPtr < MouseHi)));
            }
            if (KbdHit) {
                Matched = 1;
                if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
                    gStatIntrEvt++;
                    gIntrReportReady = 1;
                    gIntrDone = 1;
                } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                           Code == CC_STOPPED_SHORT) {
                    /*
                     * Stop EP 的副作用：勿 gIntrDone/QueueIntr，否则 Arm 同步时
                     * 会在 SetTrDeq 前再敲门铃 → Context State Error (0x13)。
                     */
                } else {
                    gStatIntrEvt++;
                    gIntrDone = 1; /* 其它错误：允许重投 */
                    if (gDiagIntrCcLogged < 4) {
                        char Line[64];
                        int n = 0;
                        const char *P = "boot: xhci kbd-intr cc=";
                        while (*P && n < 28) {
                            Line[n++] = *P++;
                        }
                        Line[n++] = (char)('0' + ((Code / 10) % 10));
                        Line[n++] = (char)('0' + (Code % 10));
                        Line[n++] = '\n';
                        Line[n] = 0;
                        ToyLogUsb(Line);
                        gDiagIntrCcLogged++;
                    }
                }
            }
            if (MouseHit) {
                Matched = 1;
                if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
                    UINT32 Remain = Evt->Status & 0xFFFFFF;
                    UINT32 Req = gMouseReportLen ? gMouseReportLen : 8;

                    gStatMouseEvt++;
                    gMouseReportReady = 1;
                    gMouseIntrDone = 1;
                    /* 短包：Remain=未传完；实际长度=请求-Remain */
                    if (Remain < Req) {
                        gMouseXferLen = (UINT8)(Req - Remain);
                    } else {
                        gMouseXferLen = (UINT8)Req;
                    }
                    if (gMouseXferLen < 3) {
                        gMouseXferLen = 3;
                    }
                    if (gMouseXferLen > 8) {
                        gMouseXferLen = 8;
                    }
                } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                           Code == CC_STOPPED_SHORT) {
                    /* 同上：Stop 取消，勿重投门铃 */
                } else {
                    gStatMouseEvt++;
                    gMouseIntrDone = 1;
                    if (gDiagIntrCcLogged < 4) {
                        char Line[64];
                        int n = 0;
                        const char *P = "boot: xhci mouse-intr cc=";
                        while (*P && n < 30) {
                            Line[n++] = *P++;
                        }
                        Line[n++] = (char)('0' + ((Code / 10) % 10));
                        Line[n++] = (char)('0' + (Code % 10));
                        Line[n++] = '\n';
                        Line[n] = 0;
                        ToyLogUsb(Line);
                        gDiagIntrCcLogged++;
                    }
                }
            }
            if (!Matched) {
                gStatUnmatched++;
                if (gDiagXferLogged < 8) {
                    char Line[80];
                    int n = 0;
                    const char *P = "boot: xhci xfer s=";
                    while (*P && n < 24) {
                        Line[n++] = *P++;
                    }
                    Line[n++] = (char)('0' + ((EvtSlot / 10) % 10));
                    Line[n++] = (char)('0' + (EvtSlot % 10));
                    Line[n++] = ' ';
                    Line[n++] = 'e';
                    Line[n++] = '=';
                    Line[n++] = (char)('0' + ((Ep / 10) % 10));
                    Line[n++] = (char)('0' + (Ep % 10));
                    Line[n++] = ' ';
                    Line[n++] = 'c';
                    Line[n++] = '=';
                    Line[n++] = (char)('0' + ((Code / 10) % 10));
                    Line[n++] = (char)('0' + (Code % 10));
                    Line[n++] = '\n';
                    Line[n] = 0;
                    ToyLogUsb(Line);
                    gDiagXferLogged++;
                }
            }
        }

        gEvtDeq++;
        if (gEvtDeq == EvtSize) {
            gEvtDeq = 0;
            gEvtCcs ^= 1;
        }
    }

    /* 无事件时勿狂写 ERDP——真机 WaitCommand 空转会 MMIO 拖死 */
    if (Progress) {
        UINT64 Erdp = PointerToPhysical(&gEvtRingLive[gEvtDeq]) | (1ULL << 3);
        WriteMmio64(gRuntimeBase + 0x38, Erdp);
        WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    }
}

/*
 * 真机：USBSTS.EINT 已置但 Cycle 对不上时，仅当环头 Cycle==~CCS 才翻一次。
 * 旧逻辑见 EINT 就翻：PCD/粘住 EINT 会把 CCS 永久弄反 → 中断完成永远吃不到，
 * 却仍可能靠碰巧/翻回来吃到部分 EP0（PHOTO：t>0 i=0）。
 */
void ProcessEventsRealPc(void) {
    UINT32 Sts;
    XHCI_TRB *Evt;
    UINT32 EvtSize = gEvtRingSize ? gEvtRingSize : EVT_SIZE;

    ProcessEvents();
    if (gCmdDone) {
        return;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    if (!(Sts & USBSTS_EINT)) {
        return;
    }
    if (gEvtDeq >= EvtSize) {
        return;
    }
    Evt = &gEvtRingLive[gEvtDeq];
    FlushDma(Evt, sizeof(*Evt));
    if ((Evt->Control & TRB_C) == ((gEvtCcs ^ 1u) & 1u)) {
        gEvtCcs ^= 1u;
        ProcessEvents();
    } else {
        /* 无待处理事件：清粘住的 EINT，勿翻 CCS */
        WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    }
}

void KbdPush(void);
void ServiceHidCompletions(void);

/* 等待命令环完成事件 */
int WaitCommand(int Timeout) {
    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = 300ULL * 3000000ULL; /* ~300ms */
        UINT64 Mid = Need / 2;
        (void)Timeout;
        while (ReadTsc() - T0 < Need) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gCmdDone) {
                return (gCmdCode == CC_SUCCESS) ? 0 : -1;
            }
            if ((ReadTsc() - T0) >= Mid) {
                Mid = Need + 1; /* 只刷一次 */
                ToyBootMarkUsb("boot: xhci cmd wait2\n");
            }
            __asm__ volatile ("pause");
        }
        return -1;
    }
    while (Timeout--) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gCmdDone) {
            return (gCmdCode == CC_SUCCESS) ? 0 : -1;
        }
    }
    return -1;
}

/* 枚举期 Wait* 也会进 ProcessEvents；必须顺带再投递中断 IN，否则 TRB 耗尽后永久无完成 */
void ServiceHidCompletions(void) {
    if (gIntrDone) {
        gIntrDone = 0;
        if (gIntrReportReady) {
            gIntrReportReady = 0;
            FlushDma(gReportBuf, sizeof(gReportBuf));
            KbdPush();
            gStatKbdPush++;
        }
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
    }
    if (gMouseIntrDone) {
        gMouseIntrDone = 0;
        if (gMouseReportReady) {
            gMouseReportReady = 0;
            FlushDma(gMouseBuf, sizeof(gMouseBuf));
            MousePush();
            gStatMousePush++;
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
    }
}

int WaitTransfer(int Timeout) {
    /*
     * 真机：按 TSC 限时（默认 ~80ms）。旧版固定 20 万次 ProcessEvents，
     * 多口 ControlXfer 超时会空转数十秒 → 短按电源无效、只能长按硬关。
     */
    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = gXferFast ? (25ULL * 3000000ULL) : (150ULL * 3000000ULL);
        for (;;) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gXferDone) {
                return (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0 : -1;
            }
            if (ReadTsc() - T0 >= Need) {
                return -1;
            }
            __asm__ volatile ("pause");
        }
    }
    while (Timeout--) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gXferDone) {
            return (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0 : -1;
        }
    }
    return -1;
}

/* 敲 Doorbell 通知硬件处理环 */
void RingDoorbell(UINT32 Slot, UINT32 Target) {
    Fence();
    WriteMmio32(gDoorbellBase + Slot * 4, Target & 0xFF);
}

/*
 * 命令超时恢复：CA 中止命令环，排空事件，再同步 enqueue。
 * 私有环可 InitRing；固件环只按 CRCR dequeue 重解析，勿盲目清环/切软环。
 */
void RecoverCommandRing(void) {
    UINT64 Cr;
    UINT64 Ptr;
    UINT32 Rcs;
    int i;
    XHCI_TRB *Base;
    UINT32 Size;
    UINT32 Enq;
    UINT32 Pcs;

    ToyBootMarkUsb("boot: xhci cmd recover\n");
    BootLog("boot: xhci command timeout, recovering...\n");

    Cr = ReadMmio64(gOperationalBase + 0x18);
    Ptr = Cr & ~0x3FULL;
    Rcs = (UINT32)(Cr & 1u);
    if (Ptr != 0) {
        WriteMmio64(gOperationalBase + 0x18, Ptr | (UINT64)(Rcs & 1u) | CRCR_CA);
        Fence();
        if (!HalCpuIsHypervisor()) {
            (void)WaitClearMs(gOperationalBase + 0x18, CRCR_CRR, 200);
        } else {
            (void)WaitClear(gOperationalBase + 0x18, CRCR_CRR, 100000);
        }
    }

    for (i = 0; i < 64; i++) {
        ProcessEvents();
    }

    Cr = ReadMmio64(gOperationalBase + 0x18);
    Ptr = Cr & ~0x3FULL;
    Rcs = (UINT32)(Cr & 1u);

    if (gCmdRingLive == gCmdRing) {
        InitRing(gCmdRing, &gCmd, RING_SIZE);
        FlushDma(gCmdRing, RING_SIZE * sizeof(XHCI_TRB));
        WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1ULL);
        Fence();
    } else if (Ptr != 0) {
        Base = 0;
        Size = 0;
        Enq = 0;
        Pcs = 0;
        if (ResolveFwCmdRing(Ptr, Rcs, &Base, &Size, &Enq, &Pcs) == 0) {
            gCmdRingLive = Base;
            gCmd.Enq = Enq;
            gCmd.Pcs = Pcs;
            gCmd.Size = Size;
        } else {
            /* 解析失败：跟 dequeue 对齐，勿切私有环再写回固件 Ptr（会踩 CRCR） */
            gCmdRingLive = (XHCI_TRB *)(UINTN)Ptr;
            gCmd.Enq = 0;
            gCmd.Pcs = Rcs & 1u;
        }
        WriteMmio64(gOperationalBase + 0x18, Ptr | (UINT64)(Rcs & 1u));
        Fence();
    }

    gCmdDone = 0;
    ToyBootMarkUsb("boot: xhci cmd ring recovered\n");
}

/* 提交一条命令 TRB 并等待完成；超时则 CA 恢复并重试一次 */
int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    int Wait = HalCpuIsHypervisor() ? 150000 : 200000;
    int RealPc = !HalCpuIsHypervisor();
    int Attempt;
    const char *Name = CmdTrbName(Control);

    for (Attempt = 0; Attempt < 2; Attempt++) {
        gCmdDone = 0;
        Enqueue(gCmdRingLive, &gCmd, Param, 0, Control | TRB_IOC);
        RingDoorbell(0, 0);
        Fence();
        if (WaitCommand(Wait) >= 0) {
            if (SlotOut) {
                *SlotOut = gCmdSlot;
            }
            /* want cc=1(Success)；got=完成码；EnableSlot 另看 slot */
            DiagChk(Name, 1, "cc=1", gCmdCode, 2);
            if (SlotOut && ((Control >> 10) & 0x3F) == TRB_ENABLE_SLOT) {
                DiagChk("EnableSlot.slot", *SlotOut != 0 && *SlotOut <= gDcbaaMaxSlot,
                        "slot=1..N", *SlotOut, 2);
            }
            return 0;
        }

        if (gCmdDone) {
            DiagChk(Name, 0, "cc=1", gCmdCode, 2);
            return -1;
        }

        DiagChk(Name, 0, "event+cc=1", RealPc ? ReadMmio32(gOperationalBase + 4) : 0, 8);
        RecoverCommandRing();
        if (Attempt == 0) {
            BootLog("xhci retry after cmd recover\n");
        }
    }
    DiagChkStr(Name, 0, "ok after retry", "fail");
    return -1;
}

UINT8 *InSlot(void) {
    return gInCtx + gCtxSize;
}

UINT8 *InEp(UINT32 Dci) {
    return gInCtx + gCtxSize * (Dci + 1);
}

/* 释放 USB 传统支持（BIOS 移交） */
void TakeLegacy(void) {
    UINT32 Hcc1 = ReadMmio32(gCapabilityBase + 0x10);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFF;
    int Wait;
    UINT32 After;

    if (Xecp == 0) {
        DiagChkStr("TakeLegacy", 0, "xECP!=0", "xECP=0");
        return;
    }
    UINT64 Ptr = gCapabilityBase + (UINT64)Xecp * 4;
    for (int i = 0; i < 64; i++) {
        UINT32 Val = ReadMmio32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFF);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFF);
        if (Id == 1) {
            WriteMmio32(Ptr, Val | (1u << 24)); /* OS Owned */
            Wait = HalCpuIsHypervisor() ? 1000000 : 50000;
            (void)WaitClear(Ptr, (1u << 16), Wait); /* BIOS Owned */
            After = ReadMmio32(Ptr);
            /* want: BIOS Owned(bit16)=0；got=完整 USBLEGSUP */
            DiagChk("TakeLegacy", !(After & (1u << 16)), "BIOS_OWN=0", After, 8);
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCapabilityBase + (UINT64)Next * 4;
    }
    /* 真机常见无 USBLEGSUP：安静模式不刷 FAIL */
    if (DiagVerbose()) {
        DiagChkStr("TakeLegacy", 0, "USBLEGSUP id=1", "not found");
    }
}

/* 真机：BootMark 直写帧缓冲（不 Present）；QEMU 正常串口/GOP */

/* 停 RS，避免无 HID 时事件环/遗留状态拖死后续 */
void HaltControllerQuiet(void) {
    UINT32 Cmd;

    if (gOperationalBase == 0) {
        return;
    }
    Cmd = ReadMmio32(gOperationalBase);
    Cmd &= ~USBCMD_RS;
    WriteMmio32(gOperationalBase, Cmd);
    (void)WaitSet(gOperationalBase + 4, USBSTS_HCH, 200000);
    /* 清 EINT；Interrupter 关 IE，降未路由 IRQ 风险 */
    WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 0);
    }
}

/* 复位 xHCI 控制器 */
int ResetController(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    Cmd &= ~USBCMD_RS;
    WriteMmio32(gOperationalBase, Cmd);
    if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        ToyLogUsb("boot: xhci halt timeout\n");
        DebugWrite("XHCI: halt timeout\n");
        return 0;
    }
    WriteMmio32(gOperationalBase, USBCMD_HCRST);
    if (!WaitClear(gOperationalBase, USBCMD_HCRST, 1000000) || !WaitClear(gOperationalBase + 4, USBSTS_CNR, 1000000)) {
        ToyLogUsb("boot: xhci reset timeout\n");
        DebugWrite("XHCI: reset timeout\n");
        return 0;
    }
    return 1;
}

/* 真机：只停 RS，不做 HCRST（该机 HCRST 后再 set RS 会挂） */
int HaltOnly(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    if (Cmd & USBCMD_RS) {
        WriteMmio32(gOperationalBase, Cmd & ~USBCMD_RS);
        if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 200000)) {
            return 0;
        }
    }
    return 1;
}

/* 真机：分阶 set RS 黄字（仅 VERBOSE） */
void BootMarkRs(char Kind, char Stage) {
    char Msg[32];
    int n = 0;
    const char *P = "boot: xhci ";
    if (!DiagVerbose()) {
        return;
    }
    while (*P) {
        Msg[n++] = *P++;
    }
    Msg[n++] = Kind; /* 'b' or 'a' */
    Msg[n++] = 'R';
    Msg[n++] = 'S';
    Msg[n++] = ' ';
    Msg[n++] = Stage;
    Msg[n++] = '\n';
    Msg[n] = 0;
    ToyBootMarkUsb(Msg);
}

/* 分配 DCBAA、建环并 Run 控制器 */
int StartController(UINT32 MaxSlots) {
    UINT32 Hcs2 = ReadMmio32(gCapabilityBase + 0x08);
    /* Linux HCS_MAX_SCRATCHPAD：Hi@25:21，Lo@31:27 */
    UINT32 Scratch = (((Hcs2 >> 21) & 0x1F) << 5) | ((Hcs2 >> 27) & 0x1F);
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Sts;
    UINT32 Hcc1;
    char B[12];
    UINT32 Si;

    Hcc1 = ReadMmio32(gCapabilityBase + 0x10);
    gDcbaaLive = gDcbaa;
    gDcbaaMaxSlot = MaxSlots;
    gDcbaaFromFirmware = 0;

    if (RealPc) {
        UINT64 FwDcbaap;
        UINTN MapBytes;
        UINT32 Slot;

        /*
         * 真机：固件 DCBAAP/scratch 保留（自建 DCBAAP 曾致 RS 挂）；
         * 命令环+事件环用私有（固件事件环无法可靠吃到 EnableSlot 完成）。
         */
        (void)Scratch;
        (void)Hcc1;
        FwDcbaap = gFwDcbaapSave ? gFwDcbaapSave
                                 : (ReadMmio64(gOperationalBase + 0x30) & ~0x3FULL);
        if (FwDcbaap == 0) {
            ToyBootMarkUsb("boot: xhci fw DCBAAP=0\n");
            return 0;
        }
        MapBytes = (UINTN)(MaxSlots + 1) * sizeof(UINT64);
        if (MapBytes < 0x1000) {
            MapBytes = 0x1000;
        }
        if (MapXhciDma(FwDcbaap, MapBytes) != 0) {
            ToyBootMarkUsb("boot: xhci map DCBAAP fail\n");
            return 0;
        }
        gDcbaaLive = (UINT64 *)(UINTN)FwDcbaap;
        gDcbaaFromFirmware = 1;
        WriteMmio32(gOperationalBase + 0x38, MaxSlots);
        for (Slot = 1; Slot <= MaxSlots; Slot++) {
            gDcbaaLive[Slot] = 0;
        }
        DcbaaFlush();
        WriteMmio64(gOperationalBase + 0x30, FwDcbaap);
        BootMarkV("boot: xhci use fw DCBAAP\n");

        /*
         * 真机：DCBAAP 必须固件（否则 RS 挂）；命令/事件环改私有。
         */
        (void)gFwCrcrSave;
        (void)gFwErstbaSave;
        (void)gFwEvtSave;
        (void)gFwErdpSave;
        gCmdRingLive = gCmdRing;
        gEvtRingLive = gEvtRing;
        gEvtRingSize = EVT_SIZE;
        InitRing(gCmdRing, &gCmd, RING_SIZE);
        FlushDma(gCmdRing, sizeof(gCmdRing));
        (void)MapXhciDma(PointerToPhysical(gCmdRing), sizeof(gCmdRing));
        WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1ULL);

        ZeroMemory(gEvtRing, sizeof(gEvtRing));
        gEvtDeq = 0;
        gEvtCcs = 1;
        ZeroMemory(gErst, sizeof(gErst));
        *(UINT64 *)(void *)gErst = PointerToPhysical(gEvtRing);
        *(UINT16 *)(void *)(gErst + 8) = (UINT16)EVT_SIZE;
        FlushDma(gEvtRing, sizeof(gEvtRing));
        FlushDma(gErst, sizeof(gErst));
        (void)MapXhciDma(PointerToPhysical(gEvtRing), sizeof(gEvtRing));
        (void)MapXhciDma(PointerToPhysical(gErst), sizeof(gErst));
        WriteMmio32(gRuntimeBase + 0x20, 0);
        WriteMmio32(gRuntimeBase + 0x24, 0);
        WriteMmio32(gRuntimeBase + 0x28, 1);
        WriteMmio32(gRuntimeBase + 0x2C, 0);
        WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
        WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));

        Fence();
        BootMarkRs('b', 'R');
        WriteMmio32(gOperationalBase, USBCMD_RS);
        Fence();
        BootMarkRs('a', 'R');
        if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 100000)) {
            DiagChk("StartController.fwRS", 0, "HCH=0", ReadMmio32(gOperationalBase + 4), 8);
            BootLog("boot: xhci run timeout\n");
            return 0;
        }
        if (!WaitSet(gOperationalBase + 0x18, CRCR_CRR, 100000)) {
            if (DiagVerbose()) {
                DiagChk("StartController.CRR", 0, "CRR=1", ReadMmio32(gOperationalBase + 0x18), 8);
                ToyBootMarkUsb("boot: xhci CRR TO\n");
            }
        } else {
            DiagChk("StartController.fwRS", 1, "HCH=0+CRR", ReadMmio32(gOperationalBase + 4), 8);
        }
        BootMarkV("boot: xhci RS running\n");
        return 1;
    }

    ZeroMemory(gDcbaa, sizeof(gDcbaa));
    ZeroMemory(gDevCtx, sizeof(gDevCtx));
    if (Scratch > 0) {
        if (Scratch > XHCI_SCRATCH_MAX) {
            BootLog("boot: xhci scratchpad >max\n");
            return 0;
        }
        BootLog("boot: xhci scratchpad=");
        HalSerialFormatHex(B, Scratch, 4);
        ToyLogUsb(B);
        ToyLogUsb("\n");
        ZeroMemory(gScratchPtr, sizeof(gScratchPtr));
        for (Si = 0; Si < Scratch; Si++) {
            gScratchPtr[Si] = PointerToPhysical(gScratchBuf[Si]);
            if (!(Hcc1 & 1u) && (gScratchPtr[Si] >> 32)) {
                return 0;
            }
            FlushDma(gScratchBuf[Si], 4096);
        }
        gDcbaa[0] = PointerToPhysical(gScratchPtr);
        FlushDma(gScratchPtr, sizeof(UINT64) * Scratch);
        BootLog("boot: xhci scratch ptrs ok\n");
    }

    BootLog("boot: xhci prog CONFIG/DCBAAP\n");
    WriteMmio32(gOperationalBase + 0x38, MaxSlots);
    FlushDma(gDcbaa, sizeof(gDcbaa));
    WriteMmio64(gOperationalBase + 0x30, PointerToPhysical(gDcbaa));

    BootLog("boot: xhci prog CRCR\n");
    InitRing(gCmdRing, &gCmd, RING_SIZE);
    FlushDma(gCmdRing, sizeof(gCmdRing));
    WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1);

    BootLog("boot: xhci prog ERST\n");
    ZeroMemory(gEvtRing, sizeof(gEvtRing));
    gEvtDeq = 0;
    gEvtCcs = 1;
    ZeroMemory(gErst, sizeof(gErst));
    *(UINT64 *)(void *)gErst = PointerToPhysical(gEvtRing);
    *(UINT16 *)(void *)(gErst + 8) = EVT_SIZE;
    FlushDma(gEvtRing, sizeof(gEvtRing));
    FlushDma(gErst, sizeof(gErst));

    WriteMmio32(gRuntimeBase + 0x20, 3);
    WriteMmio32(gRuntimeBase + 0x24, 0);
    WriteMmio32(gRuntimeBase + 0x28, 1);
    WriteMmio32(gRuntimeBase + 0x2C, 0);
    WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
    WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));

    Fence();
    Sts = ReadMmio32(gOperationalBase + 4);
    BootLog("boot: xhci USBSTS before RS=");
    HalSerialFormatHex(B, Sts, 8);
    ToyLogUsb(B);
    ToyLogUsb("\n");
    ToyBootMarkUsb("boot: xhci before RS\n");
    WriteMmio32(gOperationalBase, USBCMD_RS | USBCMD_INTE);
    Fence();
    ToyBootMarkUsb("boot: xhci after RS\n");

    if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        Sts = ReadMmio32(gOperationalBase + 4);
        DiagChk("StartController.RS", 0, "HCH=0", Sts, 8);
        BootLog("boot: xhci run timeout\n");
        return 0;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    DiagChk("StartController.RS", !(Sts & USBSTS_HCH), "HCH=0 running", Sts, 8);
    ToyBootMarkUsb("boot: xhci RS running\n");
    (void)gDcbaaFromFirmware;
    return 1;
}



int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                   int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                  UINT16 *, UINT8 *),
                   int UseBootProto) {
    gXferSlot = SlotId;
    (void)DevCtx;

    if (!HalCpuIsHypervisor()) {
        StallMs(10);
    } else {
        for (volatile int d = 0; d < 500000; d++) {
        }
    }

    /* Address 已建好本 slot 的 EP0 环；勿 InitRing/SetTrDeq 打断 Running EP0 */

    if (GetDeviceDesc() < 0) {
        return 0;
    }
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    UINT16 Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    UINT8 ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }

    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 IfaceProto = 0xFF;
    int HaveIntr = ParseFn(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval);
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    /*
     * SET_PROTOCOL(Boot) 仅对 Boot 接口（kbd Proto=1 / mouse Proto=2）。
     * QEMU usb-tablet 为 Proto=0：发 SET_PROTOCOL 会 Stall(cc=6)，EP0 随后
     * GetDesc 全失败 → 鼠标 DisableSlot，日志只有 keyboard 没有 mouse。
     */
    if (UseBootProto) {
        UINT16 Off = 0;
        while (Off + 9 <= Total) {
            UINT8 Len = gCtrlBuf[Off];
            UINT8 Type = gCtrlBuf[Off + 1];
            if (Len < 2 || Off + Len > Total) {
                break;
            }
            if (Type == 4 && Len >= 9 && gCtrlBuf[Off + 2] == Iface) {
                IfaceProto = gCtrlBuf[Off + 7];
                break;
            }
            Off = (UINT16)(Off + Len);
        }
        if (IfaceProto == 1 || IfaceProto == 2) {
            SetProtocolBoot(Iface);
        }
    }
    /* SET_IDLE(0)：部分 boot 鼠无此则中断 IN 不吐报告；与共享 EP0 环问题正交 */
    if (IfaceProto == 1 || IfaceProto == 2 || IfaceProto == 0xFF) {
        SetIdle(Iface);
    }
    return HaveIntr;
}


/* 完整 xHCI 初始化：复位、建环、枚举端口上的 USB 键盘 */
int XhciInit(UINT64 BaseAddress) {
    int RealPc = !HalCpuIsHypervisor();
    char B[12];

    if (gXhciStarted) {
        if (XhciHidKeyboardReady() || XhciMousePresent()) {
            ToyLogUsb("boot: xhci init skipped (already up)\n");
            return 1;
        }
        /* 控制器曾起但无 HID：勿假成功，否则 Probe/fallback 会挡住 PS/2 */
        ToyLogUsb("boot: xhci already up, no HID\n");
        return 0;
    }

    /* 运行时误调 / 损坏指针：QEMU 曾见 BAR=0x193A50 → Cap=0 后异常 */
    if (BaseAddress < 0x100000ULL || (BaseAddress & 0xFULL) != 0) {
        ToyLogUsb("boot: xhci reject BAR\n");
        return 0;
    }

    if (DiagVerbose()) {
        BootLog("xhci diag: VERBOSE\n");
    }
    /* 刷机核对：没有这行 = NUC 仍在跑旧 Kernel.elf */
    BootLog("boot: xhci build=kbd-v8\n");
    gCtrlFailLogged = 0;

    /*
     * 实测：白字最后停在 ports=0x12 且无 B10 黄字 → Present 在该行可能不返回。
     * 真机：一进 Init 就 mute，ports/探针全走 BootMark（直写帧缓冲）。
     */
    if (RealPc) {
        HalSerialGopMute(1);
        ToyBootMarkUsb("boot: xhci-Hhid enter\n");
        gXhciDmar = -2;
        gXhciTe = -2;
        {
            UINT64 Rsdp = HalPlatformRsdp();
            int Dmar;
            int Te;
            if (Rsdp == 0) {
                ToyBootMarkUsb("boot: xhci RSDP=0\n");
            } else {
                BootMarkV("boot: xhci RSDP ok\n");
                Dmar = AcpiTablePresent(Rsdp, "DMAR");
                gXhciDmar = Dmar;
                if (Dmar > 0) {
                    BootMarkV("boot: xhci DMAR=yes\n");
                    BootMarkV("boot: xhci TE off...\n");
                    Te = AcpiDmarDisableTranslation(Rsdp);
                    gXhciTe = Te;
                    if (Te == 2) {
                        BootMarkV("boot: xhci TE was ON->off\n");
                    } else if (Te == 1) {
                        BootMarkV("boot: xhci TE already off\n");
                    } else if (Te == 0) {
                        BootMarkV("boot: xhci TE no DRHD\n");
                    } else {
                        ToyBootMarkUsb("boot: xhci TE off fail\n");
                    }
                } else if (Dmar == 0) {
                    BootMarkV("boot: xhci DMAR=no\n");
                    gXhciTe = -2;
                } else {
                    ToyBootMarkUsb("boot: xhci DMAR=bad\n");
                }
            }
        }
    }

    if (BaseAddress == 0) {
        if (RealPc) {
            ToyBootMarkUsb("boot: xhci null BAR\n");
            HalSerialGopMute(0);
        } else {
            ToyLogUsb("boot: xhci null BAR\n");
        }
        return 0;
    }

    gCapabilityBase = BaseAddress;
    UINT32 Cap = ReadMmio32(gCapabilityBase);
    UINT32 CapLength = Cap & 0xFF;
    DiagChk("ReadCap", Cap != 0xFFFFFFFFu && CapLength >= 0x20 && CapLength != 0xFF,
            "CAP!=F.. len>=20", Cap, 8);
    if (Cap == 0xFFFFFFFFu || CapLength < 0x20 || CapLength == 0xFF) {
        if (RealPc) {
            ToyBootMarkUsb("boot: xhci bad CAP\n");
            HalSerialGopMute(0);
        } else {
            ToyLogUsb("boot: xhci bad CAP=");
            HalSerialFormatHex(B, Cap, 8);
            ToyLogUsb(B);
            ToyLogUsb("\n");
        }
        return 0;
    }

    gOperationalBase = gCapabilityBase + CapLength;
    gDoorbellBase = gCapabilityBase + (ReadMmio32(gCapabilityBase + 0x14) & ~0x3u);
    gRuntimeBase = gCapabilityBase + (ReadMmio32(gCapabilityBase + 0x18) & ~0x1Fu);
    gCtxSize = (ReadMmio32(gCapabilityBase + 0x10) & (1u << 2)) ? 64 : 32;

    /* 真机：Halt/TakeLegacy 前冻结固件环指针（其后 CRCR 常读成 0） */
    gFwDcbaapSave = 0;
    gFwCrcrSave = 0;
    gFwCrcrRcs = 1;
    gFwErstbaSave = 0;
    gFwEvtSave = 0;
    gFwEvtSegSave = 0;
    gFwErdpSave = 0;
    if (RealPc) {
        UINT8 *Erst;
        UINT64 Crcr;
        gFwDcbaapSave = ReadMmio64(gOperationalBase + 0x30) & ~0x3FULL;
        Crcr = ReadMmio64(gOperationalBase + 0x18);
        gFwCrcrSave = Crcr & ~0x3FULL;
        gFwCrcrRcs = (UINT32)(Crcr & 1ULL);
        gFwErstbaSave = ReadMmio64(gRuntimeBase + 0x30) & ~0x3FULL;
        gFwErdpSave = ReadMmio64(gRuntimeBase + 0x38);
        if (gFwErstbaSave != 0) {
            if (MapXhciDma(gFwErstbaSave, 0x1000) != 0) {
                ToyBootMarkUsb("boot: xhci map ERST fail\n");
            } else {
                Erst = (UINT8 *)(UINTN)gFwErstbaSave;
                gFwEvtSave = *(UINT64 *)(void *)Erst;
                gFwEvtSegSave = *(UINT16 *)(void *)(Erst + 8);
            }
        }
        if (gFwCrcrSave == 0 || gFwErstbaSave == 0) {
            BootMarkV("boot: xhci snap ring=0\n");
        } else {
            BootMarkV("boot: xhci snap rings ok\n");
        }
    }

    UINT32 Hcs1 = ReadMmio32(gCapabilityBase + 0x04);
    UINT32 MaxSlots = Hcs1 & 0xFF;
    gMaxPorts = (Hcs1 >> 24) & 0xFF;
    if (MaxSlots == 0) {
        MaxSlots = 1;
    }
    if (MaxSlots > DCBAA_SLOTS) {
        MaxSlots = DCBAA_SLOTS;
    }

    HalSerialFormatHex(B, gMaxPorts, 2);
    if (DiagVerbose()) {
        if (RealPc) {
            char Msg[40];
            int n = 0;
            const char *P = "boot: xhci ports=";
            while (*P && n < 28) {
                Msg[n++] = *P++;
            }
            Msg[n++] = B[0];
            Msg[n++] = B[1];
            Msg[n++] = '\n';
            Msg[n] = 0;
            ToyBootMarkUsb(Msg);
        } else {
            ToyLogUsb("boot: xhci ports=");
            ToyLogUsb(B);
            ToyLogUsb("\n");
        }
    }

    /*
     * PR-H-hub：真机不再 B14 裸 RS 后 return；HaltOnly（避免 HCRST）→ Start → 枚举。
     * 失败则 unmute，让 PS/2 有机会 Probe。
     */
    BootLogV("boot: xhci take legacy...\n");
    TakeLegacy();
    BootLogV("boot: xhci after legacy\n");

    if (RealPc) {
        BootMarkV("boot: xhci-Hhid halt\n");
        if (!HaltOnly()) {
            ToyBootMarkUsb("boot: xhci halt fail\n");
            HalSerialGopMute(0);
            ToyLogUsb("boot: xhci halt fail, desktop\n");
            return 0;
        }
        if (!StartController(MaxSlots)) {
            ToyBootMarkUsb("boot: xhci start fail\n");
            HalSerialGopMute(0);
            ToyLogUsb("boot: xhci start fail, desktop\n");
            HaltControllerQuiet();
            return 0;
        }
    } else {
        if (!ResetController() || !StartController(MaxSlots)) {
            return 0;
        }
    }
    ToyLogUsb("boot: xhci controller running\n");
    DebugWrite("XHCI: controller running\n");
    PowerConnectedPorts();

    {
        UINT32 Surveyed = 0;
        for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(p));
            if (Ps & PORTSC_CCS) {
                Surveyed++;
                DebugWrite("XHCI: survey port ");
                DebugHex32(p);
                DebugWrite(" portsc=");
                DebugHex32(Ps);
                DebugWrite("\n");
            }
        }
        {
            BootLogHex("boot: xhci CCS ports=", Surveyed, 2);
            if (Surveyed == 0) {
                EnumWhy("boot: why=no CCS\n");
            }
        }
    }

    UINT32 Port1 = 0;
    UINT8 Speed = 0;
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total = 0;

    gPortNoHid = 0;
    gPortNeedForcePr = 0;
    {
        int PassMax = 3;
        for (int Wait = 0; Wait < PassMax && Port1 == 0; Wait++) {
            if (DiagVerbose()) {
                ToyLogUsb("boot: xhci enum pass=");
                {
                    char B[12];
                    HalSerialFormatHex(B, (UINT64)(UINT32)(Wait + 1), 2);
                    ToyLogUsb(B);
                    ToyLogUsb("\n");
                }
            }
            for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
                UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(p));
                if (!(Ps & PORTSC_CCS)) {
                    continue;
                }
                BootLogHexV("boot: xhci try port=", p, 2);
                if (!ResetPort(p)) {
                    continue;
                }
                UINT32 After = ReadMmio32(gOperationalBase + PortReg(p));
                Speed = PortSpeed(After);
                gPort1 = p;
                gSpeed = Speed;

                BootMarkV("boot: xhci address...\n");
                if (!AddressDevice(p, Speed)) {
                    ToyBootMarkUsb("boot: xhci addr fail\n");
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                BootMarkV("boot: xhci address ok\n");

                BootMarkV("boot: xhci get desc\n");
                if (GetDeviceDesc() < 0) {
                    ToyBootMarkUsb("boot: xhci desc fail\n");
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                /* PR-H-hub：根口 hub（device class 9）→ 子口找键盘 */
                if (IsHubDeviceDesc()) {
                    BootLog("boot: xhci hub root\n");
                    if (TryHubOnRootPort(p, Speed)) {
                        Port1 = gPort1;
                        break;
                    }
                    EnumWhy("boot: why=hub fail\n");
                    DisableSlot(gHubSlotId);
                    continue;
                }
                if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
                    EnumWhy("boot: why=cfg desc\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                {
                    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
                    if (Total < 9) {
                        Total = 9;
                    }
                    if (Total > sizeof(gCtrlBuf)) {
                        Total = (UINT16)sizeof(gCtrlBuf);
                    }
                    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
                        EnumWhy("boot: why=cfg desc\n");
                        DisableSlot(gSlotId);
                        continue;
                    }
                    /*
                     * bDeviceClass=0 的 hub：配置里 Interface Class=9。
                     * 家侧 port3「no hid ep」即此类；不进 hub 则真鼠标可能在 hub 后。
                     */
                    if (ConfigHasHubIface(gCtrlBuf, Total)) {
                        BootLog("boot: xhci hub (iface class 9)\n");
                        if (TryHubOnRootPort(p, Speed)) {
                            Port1 = gPort1;
                            break;
                        }
                        EnumWhy("boot: why=hub iface fail\n");
                        DisableSlot(gHubSlotId);
                        continue;
                    }
                    ConfigVal = gCtrlBuf[5];
                    if (ConfigVal == 0) {
                        ConfigVal = 1;
                    }
                    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps,
                                           &Interval);
                }
                if (!HaveIntr) {
                    EnumWhy("boot: why=no hid ep\n");
                    gPortNoHid |= (1u << p);
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
                    /* 优先原地认领为鼠；失败才 Disable，留给 InitMouseOnPort+ForcePR */
                    if (ClaimAddressedSlotAsMouse(p, Speed, Total, ConfigVal)) {
                        continue;
                    }
                    gPortNeedForcePr |= (1u << p);
                    DisableSlot(gSlotId);
                    continue;
                }
                if (SetConfig(ConfigVal) < 0) {
                    EnumWhy("boot: why=set cfg\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                (void)SetProtocolBoot(gKbdIface);
                SetIdle(gKbdIface);
                {
                    UINT8 MEp = 0, MIv = 10;
                    UINT16 MMps = 8;
                    int WantMouse;

                    WantMouse = HalCpuIsHypervisor() &&
                                PrepCompositeMouse(Total, Speed, gKbdIface, EpAddr, &MEp, &MMps,
                                                   &MIv);
                    if (!HalCpuIsHypervisor()) {
                        /* 真机 v6：仅键盘对照；复合鼠会弄死键 IN（v5 Sync ok 仍 k=0） */
                        if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                            DisableSlot(gSlotId);
                            continue;
                        }
                        ZeroMemory(gReportBuf, 8);
                        QueueIntr();
                        BootLog("boot: xhci kbd-only then bind mouse ports\n");
                        BootLog("boot: xhci kbd-fix=v8\n");
                    } else if (WantMouse) {
                        (void)SetInterface(gKbdIface, 0);
                        (void)SetProtocolBoot(gKbdIface);
                        SetIdle(gKbdIface);
                        if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, MEp, MMps, MIv)) {
                            DisableSlot(gSlotId);
                            continue;
                        }
                        ZeroMemory(gReportBuf, 8);
                        QueueIntr();
                        QueueMouseIntr();
                        BootLog("boot: xhci-hid mouse (composite)\n");
                    } else {
                        if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                            DisableSlot(gSlotId);
                            continue;
                        }
                        ZeroMemory(gReportBuf, 8);
                        QueueIntr();
                    }
                }
                gUseGetReport = 0;
                Port1 = p;
                break;
            }
            if (Port1 == 0) {
                if (RealPc) {
                    StallMs(50);
                } else {
                    for (volatile int d = 0; d < 40000; d++) {
                    }
                }
            }
        }
    }

    if (Port1 == 0) {
        if (RealPc) {
            HalSerialGopMute(0); /* 放弃 xHCI：允许后续 boot 黄字 */
        }
        ToyLogUsb("boot: xhci up but no HID keyboard\n");
        BootLog("boot: xhci up but no HID keyboard\n");
        if (gEnumWhy) {
            BootLog(gEnumWhy);
        }
        DebugWrite("XHCI: no keyboard\n");
        for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
            if (InitMouseOnPort(p)) {
                BootLog("boot: xhci mouse only\n");
                break;
            }
        }
        gXhciStarted = 1;
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    /* 与 mouse 同走 BootLog：真机屏上先 keyboard 再 mouse，再由 Probe 打 init returned */
    BootLog("boot: xhci-hid keyboard\n");

    /*
     * 真机有线键鼠：优先其它口独立鼠（两 slot，利于保键盘）。
     * 若独立口失败，再回退同 slot 复合——保证现在能用的鼠标不丢。
     */
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 p = 1; p <= gMaxPorts; p++) {
            if (p == gPort1) {
                continue;
            }
            if (InitMouseOnPort(p)) {
                BootLog("boot: xhci mouse on other port\n");
                break;
            }
        }
    }
    if (gMouseSlotId == 0) {
        BootLog("boot: xhci mouse fallback composite-on-kbd\n");
        (void)InitMouseOnKeyboardSlot();
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }

    gXhciStarted = 1;
    return 1;
}


int XhciHidKeyboardReady(void) {
    return gSlotId != 0;
}

/*
 * 刀：笔记本可能有多颗 xHCI；第一颗 8 口全 RxDetect/无 CCS 时放弃，
 * 清 gXhciStarted 才能对下一 BAR 再跑 XhciInit。NUC/工控有 HID 不会走到这里。
 */
void XhciAbandonNoHid(void) {
    HaltControllerQuiet();
    gSlotId = 0;
    gMouseSlotId = 0;
    gHubSlotId = 0;
    gIntrDci = 0;
    gMouseIntrDci = 0;
    gPort1 = 0;
    gXhciStarted = 0;
    BootLog("boot: xhci abandon no HID\n");
}

/* 将键盘报告推入环形软件队列 */
void KbdPush(void) {
    UINT32 Next = (gKeyboardWriteIndex + 1) % KBD_Q;
    if (Next == gKeyboardReadIndex) {
        return;
    }
    UINT8 *Dst = (UINT8 *)&gKbdQ[gKeyboardWriteIndex];
    for (int i = 0; i < 8; i++) {
        Dst[i] = gReportBuf[i];
    }
    gKeyboardWriteIndex = Next;
}




int XhciKeyboardSetLeds(UINT8 Leds) {
    UINT8 LedByte = Leds;

    if (gSlotId == 0) {
        return -1;
    }
    gXferSlot = gSlotId;
    return SetReportOutput(gKbdIface, &LedByte, 1);
}

/* 从键盘报告队列取一条，有数据返回 1，空队列返回 0 */
int XhciDequeueKeyboard(USB_KEYBOARD_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gHidQueueLock);
    if (gKeyboardReadIndex != gKeyboardWriteIndex) {
        UINT8 *Src = (UINT8 *)&gKbdQ[gKeyboardReadIndex];
        UINT8 *Dst = (UINT8 *)Report;
        for (int i = 0; i < 8; i++) {
            Dst[i] = Src[i];
        }
        gKeyboardReadIndex = (gKeyboardReadIndex + 1) % KBD_Q;
        Ok = 1;
    }
    SpinLockRelease(&gHidQueueLock);
    return Ok;
}


/*
 * PR-H-msc-2/4：Bulk 环 Init；claim 后 Ready=1（仍无 SCSI）。
 */
int XhciMscBringUp(void) {
    if (!gMscBulkRingsInited) {
        InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
        InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);
        FlushDma(gBulkInRing, sizeof(gBulkInRing));
        FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
        gMscBulkRingsInited = 1;
    }
    return gMscClaimed ? 0 : -1;
}

int XhciMscReady(void) {
    return gMscClaimed ? 1 : 0;
}

static int WaitBulk(void) {
    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = 500ULL * 3000000ULL; /* ~500ms：大 U 盘 INQUIRY 可慢 */

        for (;;) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gBulkDone) {
                return (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
                                                                                : -1;
            }
            if (ReadTsc() - T0 >= Need) {
                return -1;
            }
            __asm__ volatile ("pause");
        }
    }
    {
        int Timeout = 200000;

        while (Timeout--) {
            ProcessEvents();
            ServiceHidCompletions();
            if (gBulkDone) {
                return (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
                                                                                : -1;
            }
        }
    }
    return -1;
}

/*
 * PR-H-msc-5：Bulk 普通传输。DirIn=1 → Bulk IN 环；0 → Bulk OUT。
 * 成功 0；失败 -1。短包算成功（CSW/INQUIRY 常见）。
 */
int XhciBulkXfer(int DirIn, void *Buf, UINT32 Len) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT32 Dci;
    UINT32 Ctrl;

    if (!gMscClaimed || gMscScanSlot == 0 || Buf == 0 || Len == 0) {
        return -1;
    }
    if (gMscBulkInDci == 0 || gMscBulkOutDci == 0) {
        return -1;
    }

    if (DirIn) {
        Ring = gBulkInRing;
        St = &gBulkIn;
        Dci = gMscBulkInDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP;
    } else {
        Ring = gBulkOutRing;
        St = &gBulkOut;
        Dci = gMscBulkOutDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
    }

    FlushDma(Buf, Len);
    gBulkDone = 0;
    gBulkCode = 0;
    gBulkRemain = 0;
    Enqueue(Ring, St, PointerToPhysical(Buf), Len, Ctrl);
    FlushDma(Ring, sizeof(XHCI_TRB) * (St->Size ? St->Size : RING_SIZE));
    RingDoorbell(gMscScanSlot, Dci);
    if (WaitBulk() < 0) {
        BootLogHex("boot: msc bulk fail cc=", gBulkCode, 2);
        return -1;
    }
    FlushDma(Buf, Len);
    return 0;
}

/*
 * BOT：CBW → [DATA] → CSW。DataIn=1 时数据走 Bulk IN。
 * 成功 0；失败 -1（不 ClearHalt，留给后续刀）。
 */
static int MscBot(UINT8 *CbwCb, UINT8 CbLen, UINT32 DataLen, int DataIn,
                  void *Data) {
    UINT8 Cbw[32] __attribute__((aligned(64)));
    UINT8 Csw[16] __attribute__((aligned(64)));
    static UINT32 Tag;
    UINT32 i;

    if (!gMscClaimed) {
        return -1;
    }
    if (CbLen == 0 || CbLen > 16) {
        return -1;
    }
    if (DataLen != 0 && Data == 0) {
        return -1;
    }

    Tag++;
    ZeroMemory(Cbw, sizeof(Cbw));
    Cbw[0] = 0x55;
    Cbw[1] = 0x53;
    Cbw[2] = 0x42;
    Cbw[3] = 0x43; /* USBC */
    Cbw[4] = (UINT8)(Tag);
    Cbw[5] = (UINT8)(Tag >> 8);
    Cbw[6] = (UINT8)(Tag >> 16);
    Cbw[7] = (UINT8)(Tag >> 24);
    Cbw[8] = (UINT8)(DataLen);
    Cbw[9] = (UINT8)(DataLen >> 8);
    Cbw[10] = (UINT8)(DataLen >> 16);
    Cbw[11] = (UINT8)(DataLen >> 24);
    Cbw[12] = DataIn ? 0x80u : 0x00u;
    Cbw[13] = 0; /* LUN */
    Cbw[14] = CbLen;
    for (i = 0; i < CbLen; i++) {
        Cbw[15 + i] = CbwCb[i];
    }

    if (XhciBulkXfer(0, Cbw, 31) < 0) {
        BootLog("boot: msc bot cbw fail\n");
        return -1;
    }
    if (DataLen != 0) {
        if (XhciBulkXfer(DataIn ? 1 : 0, Data, DataLen) < 0) {
            BootLog("boot: msc bot data fail\n");
            return -1;
        }
    }
    ZeroMemory(Csw, sizeof(Csw));
    if (XhciBulkXfer(1, Csw, 13) < 0) {
        BootLog("boot: msc bot csw fail\n");
        return -1;
    }
    /* USBS */
    if (Csw[0] != 0x55 || Csw[1] != 0x53 || Csw[2] != 0x42 || Csw[3] != 0x53) {
        BootLog("boot: msc bot csw sig\n");
        return -1;
    }
    if (Csw[12] != 0) {
        BootLogHex("boot: msc bot status=", Csw[12], 2);
        return -1;
    }
    return 0;
}

/*
 * PR-H-msc-5：INQUIRY + READ CAPACITY(10)。不读分区、不挂 FAT。
 * 成功 0 并填 gMscBlockCount/Size；失败 -1。
 */
int XhciMscCapacity(void) {
    UINT8 Inquiry[36] __attribute__((aligned(64)));
    UINT8 Cap[8] __attribute__((aligned(64)));
    UINT8 Cdb[16];
    UINT32 LastLba;
    UINT32 Bsz;

    if (!gMscClaimed || gMscScanSlot == 0) {
        BootLog("boot: msc capacity not claimed\n");
        return -1;
    }

    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    ZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = 0x12; /* INQUIRY */
    Cdb[4] = 36;
    ZeroMemory(Inquiry, sizeof(Inquiry));
    if (MscBot(Cdb, 6, 36, 1, Inquiry) < 0) {
        BootLog("boot: msc inquiry fail\n");
        return -1;
    }
    BootLogHex("boot: msc inquiry pdt=", Inquiry[0] & 0x1Fu, 2);
    {
        char Line[48];
        int n = 0;
        const char *P = "boot: msc vendor=";
        int i;

        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        for (i = 8; i < 16 && n < 46; i++) {
            char C = (char)Inquiry[i];

            if (C < 32 || C > 126) {
                C = '.';
            }
            Line[n++] = C;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }

    ZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = 0x25; /* READ CAPACITY(10) */
    ZeroMemory(Cap, sizeof(Cap));
    if (MscBot(Cdb, 10, 8, 1, Cap) < 0) {
        BootLog("boot: msc readcap fail\n");
        return -1;
    }
    LastLba = ((UINT32)Cap[0] << 24) | ((UINT32)Cap[1] << 16) |
              ((UINT32)Cap[2] << 8) | (UINT32)Cap[3];
    Bsz = ((UINT32)Cap[4] << 24) | ((UINT32)Cap[5] << 16) |
          ((UINT32)Cap[6] << 8) | (UINT32)Cap[7];
    if (Bsz == 0) {
        BootLog("boot: msc readcap bsz0\n");
        return -1;
    }
    gMscBlockCount = LastLba + 1u;
    gMscBlockSize = Bsz;
    gMscCapacityOk = 1;
    BootLogHex("boot: msc blocks=", gMscBlockCount, 8);
    BootLogHex("boot: msc bsize=", gMscBlockSize, 8);
    return 0;
}

UINT32 XhciMscBlockCount(void) {
    return gMscCapacityOk ? gMscBlockCount : 0;
}

UINT32 XhciMscBlockSize(void) {
    return gMscCapacityOk ? gMscBlockSize : 0;
}

/*
 * PR-H-msc-6：BOT READ(10)。仅支持 512B 逻辑块（与 BLOCK_SECTOR_SIZE 对齐）。
 * 成功 1；失败 0。
 */
int XhciMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT8 *Ptr = (UINT8 *)Buffer;
    UINT32 Done = 0;
    UINT32 Bsz;

    if (!Buffer || Count == 0) {
        return 0;
    }
    if (!gMscClaimed || gMscScanSlot == 0) {
        return 0;
    }
    if (!gMscCapacityOk) {
        if (XhciMscCapacity() != 0) {
            return 0;
        }
    }
    Bsz = gMscBlockSize;
    if (Bsz != 512u) {
        BootLogHex("boot: msc read bad bsize=", Bsz, 8);
        return 0;
    }
    if (Lba >= gMscBlockCount) {
        return 0;
    }
    if (Count > gMscBlockCount - Lba) {
        Count = gMscBlockCount - Lba;
    }

    while (Done < Count) {
        UINT8 Cdb[16];
        UINT32 Chunk = Count - Done;
        UINT32 Bytes;
        UINT32 CurLba = Lba + Done;

        /* 单次 BOT 数据 ≤ 4KiB，减轻 Bulk 超时 */
        if (Chunk > 8u) {
            Chunk = 8u;
        }
        Bytes = Chunk * Bsz;
        ZeroMemory(Cdb, sizeof(Cdb));
        Cdb[0] = 0x28; /* READ(10) */
        Cdb[2] = (UINT8)(CurLba >> 24);
        Cdb[3] = (UINT8)(CurLba >> 16);
        Cdb[4] = (UINT8)(CurLba >> 8);
        Cdb[5] = (UINT8)(CurLba);
        Cdb[7] = (UINT8)(Chunk >> 8);
        Cdb[8] = (UINT8)(Chunk);
        if (MscBot(Cdb, 10, Bytes, 1, Ptr + (Done * Bsz)) < 0) {
            BootLogHex("boot: msc read10 fail lba=", CurLba, 8);
            return 0;
        }
        Done += Chunk;
    }
    return 1;
}

/* 配置描述符中找 MSC Bulk IN/OUT（偏好 BOT；允许 UASP；兜底任一对 Bulk） */
static int ParseMscBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutIface,
                       UINT8 *EpIn, UINT16 *MpsIn, UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 IfaceClass = 0;
    UINT8 IfaceSub = 0;
    UINT8 IfaceProto = 0;
    UINT8 BestIface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;
    int BestScore = -1;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurIface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            IfaceClass = Cfg[Off + 5];
            IfaceSub = Cfg[Off + 6];
            IfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            /* class8 BOT/UASP 优先；其它 iface 仅作兜底（score 0） */
            if (IfaceClass == 0x08) {
                Score = 10;
                if (IfaceSub == 0x06) {
                    Score += 2;
                }
                if (IfaceProto == 0x50 || IfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestIface = CurIface;
                BestIn = 0;
                BestOut = 0;
                BestInMps = 0;
                BestOutMps = 0;
            } else if (CurIface != BestIface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMps = Mps ? Mps : 64;
            } else {
                BestOut = Addr;
                BestOutMps = Mps ? Mps : 64;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    /* 兜底 score=0 须成对 Bulk；class8 同 */
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (BestScore < 0) {
        return 0;
    }
    /* 无 class8 时仅当找到成对 Bulk 才接受（score 0） */
    if (OutIface) {
        *OutIface = BestIface;
    }
    if (EpIn) {
        *EpIn = BestIn;
    }
    if (MpsIn) {
        *MpsIn = BestInMps;
    }
    if (EpOut) {
        *EpOut = BestOut;
    }
    if (MpsOut) {
        *MpsOut = BestOutMps;
    }
    return 1;
}

static void LogMscCfgIfaces(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;
    int N = 0;

    while (Off + 9 <= Total && N < 6) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            BootLogHex("boot: msc claim iface=", Cfg[Off + 2], 2);
            BootLogHex("boot: msc claim iclass=", Cfg[Off + 5], 2);
            BootLogHex("boot: msc claim isub=", Cfg[Off + 6], 2);
            BootLogHex("boot: msc claim iproto=", Cfg[Off + 7], 2);
            N++;
        }
        Off = (UINT16)(Off + Len);
    }
}

/* ConfigEP：Bulk IN + Bulk OUT（EP Type 6/2）；带回 Route/TT */
static int ConfigureMscBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                            UINT8 EpIn, UINT16 MpsIn, UINT8 EpOut, UINT16 MpsOut) {
    UINT8 InNum = EpIn & 0x0F;
    UINT8 OutNum = EpOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;

    if (MpsIn == 0 || MpsIn > 1024) {
        MpsIn = 512;
    }
    if (MpsOut == 0 || MpsOut > 1024) {
        MpsOut = 512;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gMscRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gMscHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gMscHubSlot | ((UINT32)gMscTtPort << 8);
    }

    if (!gMscBulkRingsInited) {
        (void)XhciMscBringUp();
    }
    InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
    InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)MpsIn << 16); /* Bulk IN */
    Deq = PointerToPhysical(gBulkInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsIn;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16); /* Bulk OUT */
    Deq = PointerToPhysical(gBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gBulkInRing, sizeof(gBulkInRing));
    FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
    FlushDma(gMscScanDevCtx, sizeof(gMscScanDevCtx));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        BootLog("boot: msc claim cfg ep fail\n");
        return 0;
    }

    gMscBulkInDci = InDci;
    gMscBulkOutDci = OutDci;
    gMscBulkInMps = MpsIn;
    gMscBulkOutMps = MpsOut;
    return 1;
}

/*
 * gMscScanSlot 已 Address：取配置、Parse Bulk、SetConfig、ConfigEP。
 * 成功置 gMscClaimed；失败 Disable slot。
 */
int XhciMscFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT8 Iface = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 DevClass;

    if (gMscScanSlot == 0) {
        return 0;
    }
    gXferSlot = gMscScanSlot;
    if (GetDeviceDesc() < 0) {
        BootLog("boot: msc claim desc fail\n");
        goto fail;
    }
    DevClass = gCtrlBuf[4];
    BootLogHex("boot: msc claim dclass=", DevClass, 2);
    if (DevClass == 0x09) {
        BootLog("boot: msc claim skip hub device\n");
        goto fail;
    }
    if (DevClass == 0x03 || DevClass == 0xE0) {
        BootLogHex("boot: msc claim skip class=", DevClass, 2);
        goto fail;
    }

    if (GetDesc(0x0200, 0, 9, gMscCfgBuf) < 0) {
        BootLog("boot: msc claim cfg9 fail\n");
        goto fail;
    }
    Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gMscCfgBuf)) {
        BootLogHex("boot: msc claim cfg trunc want=", Total, 4);
        Total = (UINT16)sizeof(gMscCfgBuf);
    }
    ConfigVal = gMscCfgBuf[5] ? gMscCfgBuf[5] : 1;
    if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
        RecoverEp0(gMscScanSlot);
        gXferSlot = gMscScanSlot;
        if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
            BootLog("boot: msc claim cfg fail\n");
            goto fail;
        }
    }
    BootLogHex("boot: msc claim cfg len=", Total, 4);

    if (ConfigHasHubIface(gMscCfgBuf, Total)) {
        /* 根口应在 ClaimPorts 已认领；子口嵌套 hub 跳过 */
        BootLogHex("boot: msc claim cfg hub iface port=", RootPort, 2);
        goto fail;
    }

    if (!ParseMscBulk(gMscCfgBuf, Total, &Iface, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        BootLogHex("boot: msc claim no bulk port=", RootPort, 2);
        LogMscCfgIfaces(gMscCfgBuf, Total);
        goto fail;
    }

    if (SetConfig(ConfigVal) < 0) {
        BootLog("boot: msc claim setcfg fail\n");
        goto fail;
    }

    if (!ConfigureMscBulk(gMscScanSlot, RootPort, Speed, EpIn, MpsIn, EpOut, MpsOut)) {
        goto fail;
    }

    gMscPort = RootPort;
    gMscClaimed = 1;
    BootLogHex("boot: msc claim ok port=", RootPort, 2);
    BootLogHex("boot: msc claim slot=", gMscScanSlot, 2);
    BootLogHex("boot: msc claim iface=", Iface, 2);
    BootLogHex("boot: msc claim epin=", EpIn, 2);
    BootLogHex("boot: msc claim epout=", EpOut, 2);
    BootLogHex("boot: msc claim route=", gMscRoute, 2);
    BootLog("boot: msc claim bulk ok\n");
    return 1;

fail:
    if (gMscScanSlot != 0) {
        gPortNeedForcePr |= (1u << RootPort);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    return 0;
}

/*
 * PR-H-msc-3：扫根口；跳过键鼠/hub 口；已 PED 则 Address+读 class 后 DisableSlot。
 * 不 Force PR、不 SetConfig、不 BOT；独立 EP0 环，不碰键鼠环。
 * 返回：打到 class 日志的口数；HC 未起则 -1。
 */
int XhciMscScanPorts(void) {
    UINT32 P;
    int Found = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("boot: msc scan no hc\n");
        return -1;
    }

    BootLog("boot: msc scan begin\n");
    if (gMscScanSlot != 0 && !gMscClaimed) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));
        UINT8 Speed;
        UINT8 Class;
        UINT8 Sub;
        UINT8 Proto;
        UINT16 Vid;
        UINT16 Pid;
        USB_DEVICE_DESCRIPTOR *Dev;

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            BootLogHex("boot: msc scan skip kbd port=", P, 2);
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            BootLogHex("boot: msc scan skip mouse port=", P, 2);
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            BootLogHex("boot: msc scan skip hub port=", P, 2);
            continue;
        }
        if (gMscClaimed && P == gMscPort) {
            BootLogHex("boot: msc scan skip claimed port=", P, 2);
            continue;
        }
        if (!(Ps & PORTSC_PED)) {
            /* 故意不 Force PR：留给 msc-4 claim */
            BootLogHex("boot: msc scan skip not PED port=", P, 2);
            continue;
        }

        Speed = PortSpeed(Ps);
        if (!AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx, 0, 0, 0, 0,
                                 0)) {
            BootLogHex("boot: msc scan addr fail port=", P, 2);
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            continue;
        }
        if (gMscScanSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
        }

        gXferSlot = gMscScanSlot;
        if (GetDeviceDesc() < 0) {
            BootLogHex("boot: msc scan desc fail port=", P, 2);
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
            continue;
        }

        Dev = (USB_DEVICE_DESCRIPTOR *)(void *)gCtrlBuf;
        Class = Dev->bDeviceClass;
        Sub = Dev->bDeviceSubClass;
        Proto = Dev->bDeviceProtocol;
        Vid = Dev->idVendor;
        Pid = Dev->idProduct;

        if (Class == 0) {
            if (GetDesc(0x0200, 0, 9, gCtrlBuf) == 0) {
                UINT16 Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
                if (Total < 9) {
                    Total = 9;
                }
                if (Total > sizeof(gCtrlBuf)) {
                    Total = (UINT16)sizeof(gCtrlBuf);
                }
                if (GetDesc(0x0200, 0, Total, gCtrlBuf) == 0) {
                    UINT16 Off = 0;
                    while (Off + 9 <= Total) {
                        UINT8 Len = gCtrlBuf[Off];
                        UINT8 Type = gCtrlBuf[Off + 1];
                        if (Len < 2 || Off + Len > Total) {
                            break;
                        }
                        if (Type == 4 && Len >= 9) {
                            Class = gCtrlBuf[Off + 5];
                            Sub = gCtrlBuf[Off + 6];
                            Proto = gCtrlBuf[Off + 7];
                            break;
                        }
                        Off = (UINT16)(Off + Len);
                    }
                }
            }
        }

        BootLogHex("boot: msc scan port=", P, 2);
        BootLogHex("boot: msc scan class=", Class, 2);
        BootLogHex("boot: msc scan sub=", Sub, 2);
        BootLogHex("boot: msc scan proto=", Proto, 2);
        BootLogHex("boot: msc scan vid=", Vid, 4);
        BootLogHex("boot: msc scan pid=", Pid, 4);
        Found++;

        /* Disable 后口常仍 PED；下次 claim/Address 须 Force PR，否则 cc=0x04 */
        gPortNeedForcePr |= (1u << P);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }

    BootLogHex("boot: msc scan done n=", (UINT32)Found, 2);
    return Found;
}

/*
 * PR-H-msc-4：单口 claim — Address（可 Force PR）+ SetConfig + Bulk IN/OUT。
 * 优先扫已有 hub 子口（键鼠经 hub 时 U 盘常在同 hub）；再扫其它根口。
 * 不 SCSI、不挂 FAT、不碰键鼠口。成功则保留 slot；Ready=1。
 */
int XhciMscClaimPorts(void) {
    UINT32 P;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("boot: msc claim no hc\n");
        return -1;
    }
    if (gMscClaimed && gMscScanSlot != 0) {
        BootLogHex("boot: msc claim already port=", gMscPort, 2);
        return 1;
    }

    BootLog("boot: msc claim begin\n");
    (void)XhciMscBringUp();

    if (gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    gMscClaimed = 0;
    gMscPort = 0;
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    /* 键盘已走 hub：先扫子口找 MSC，勿跳过整颗 hub 根口 */
    if (gHubSlotId != 0 && EnumHubChildrenForMsc()) {
        return 1;
    }

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps;
        UINT8 Speed;
        int Force;
        int AddrOk;
        UINT32 QuietSave;

        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            continue; /* 子口已在上面 EnumHubChildrenForMsc 试过 */
        }

        Force = (gPortNeedForcePr & (1u << P)) ? 1 : 0;
        if (!(Ps & PORTSC_PED)) {
            Force = 1;
        }
        BootLogHex(Force ? "boot: msc claim reset force port="
                         : "boot: msc claim reset port=",
                   P, 2);
        if (!ResetPortEx(P, Force)) {
            BootLogHex("boot: msc claim reset fail port=", P, 2);
            continue;
        }
        if (Force && !HalCpuIsHypervisor()) {
            StallMs(20);
        }
        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (!(Ps & PORTSC_PED) || !(Ps & PORTSC_CCS)) {
            BootLogHex("boot: msc claim not PED port=", P, 2);
            continue;
        }

        Speed = PortSpeed(Ps);
        QuietSave = gDiagQuiet;
        gDiagQuiet = 1;
        AddrOk = AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx, 0, 0, 0, 0,
                                     0);
        if (!AddrOk && gMscScanSlot != 0) {
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
        }
        if (!AddrOk && !Force && (gCmdCode == 4 || gCmdCode == 0x11)) {
            BootLogHex("boot: msc claim addr retry force port=", P, 2);
            gPortNeedForcePr |= (1u << P);
            if (ResetPortEx(P, 1)) {
                if (!HalCpuIsHypervisor()) {
                    StallMs(20);
                }
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
                AddrOk = AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx,
                                             0, 0, 0, 0, 0);
                if (!AddrOk && gMscScanSlot != 0) {
                    DisableSlot(gMscScanSlot);
                    gMscScanSlot = 0;
                }
            }
        }
        gDiagQuiet = QuietSave;
        if (!AddrOk) {
            BootLogHex("boot: msc claim addr fail port=", P, 2);
            BootLogHex("boot: msc claim addr cc=", gCmdCode, 2);
            gPortNeedForcePr |= (1u << P);
            continue;
        }
        if (gMscScanSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
        }
        gPortNeedForcePr &= ~(1u << P);
        gMscRoute = 0;
        gMscHubSlot = 0;
        gMscTtPort = 0;

        gXferSlot = gMscScanSlot;
        if (GetDeviceDesc() < 0) {
            BootLogHex("boot: msc claim desc fail port=", P, 2);
            gPortNeedForcePr |= (1u << P);
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
            continue;
        }

        /*
         * 根口 hub：device class 9，或 class 0 但配置含 hub iface（真机常见）。
         * 认领后扫子口 MSC；本刀新认领且无 MSC 则释放，以便试下一根口 hub。
         */
        {
            int Hubish = IsHubDeviceDesc() || (gCtrlBuf[4] == 0x09);

            if (!Hubish && gCtrlBuf[4] == 0) {
                UINT16 Total;

                if (GetDesc(0x0200, 0, 9, gMscCfgBuf) == 0) {
                    Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
                    if (Total < 9) {
                        Total = 9;
                    }
                    if (Total > sizeof(gMscCfgBuf)) {
                        Total = (UINT16)sizeof(gMscCfgBuf);
                    }
                    if (GetDesc(0x0200, 0, Total, gMscCfgBuf) == 0 &&
                        ConfigHasHubIface(gMscCfgBuf, Total)) {
                        Hubish = 1;
                        BootLogHex("boot: msc claim hub iface root=", P, 2);
                    }
                } else {
                    RecoverEp0(gMscScanSlot);
                    gXferSlot = gMscScanSlot;
                }
            }

            if (Hubish) {
                UINT32 Was = gMscScanSlot;
                UINT32 HubBefore = gHubSlotId;

                gMscScanSlot = 0;
                BootLogHex("boot: msc claim hub on root=", P, 2);
                /*
                 * 已有 HID hub 时 ClaimHubOnRootPort 会 DisableSlot(Was)，
                 * 正是外接第二 hub（U 盘所在）→ none + 长时间 Stall 像卡死。
                 */
                if (HubBefore != 0 && Was != 0 && Was != HubBefore) {
                    if (ProbeSecondHubForMsc(Was, P, Speed)) {
                        return 1;
                    }
                    continue;
                }
                if (ClaimHubOnRootPort(P, Speed, Was)) {
                    if (EnumHubChildrenForMsc()) {
                        return 1;
                    }
                    /* HID 未占用此 hub：无 MSC 则放掉，试其它根口 */
                    if (HubBefore == 0 && gHubSlotId != 0 && gHubRootPort == P) {
                        DisableSlot(gHubSlotId);
                        gHubSlotId = 0;
                        gHubRootPort = 0;
                        BootLog("boot: msc claim hub no msc, release\n");
                    }
                } else if (Was != 0) {
                    DisableSlot(Was);
                }
                continue;
            }
        }

        /* FinishClaim 会再 GetDeviceDesc；描述已在 gCtrlBuf，直接走配置 */
        if (XhciMscFinishClaim(P, Speed)) {
            return 1;
        }
    }

    BootLog("boot: msc claim none\n");
    return 0;
}
