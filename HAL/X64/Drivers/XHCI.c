/*
 * XHCI.c — xHCI 主机控制器与 USB 键盘驱动
 *
 * 实现命令环/事件环、Enable Slot、Address Device、EP0 控制传输、
 * 中断端点 IN 轮询。键盘报告入队后由 XhciDequeueKeyboard 取出。
 *
 * 主要静态辅助函数：
 *   ReadMmio32/WriteMmio32/WriteMmio64/Phys/Fence/Zero — MMIO 与内存工具
 *   InitRing/Enqueue/ProcessEvents — TRB 环管理
 *   ResetController/StartController — 控制器生命周期
 *   ResetPort/AddressDevice — 端口与设备枚举
 *   ControlXfer/GetDesc/SetConfig — USB 控制传输
 *   ConfigureIntr/QueueIntr/ParseConfig — HID 中断端点
 *
 * 对外 API：
 *   XhciInit        — 完整初始化并枚举键盘
 *   XhciEnableIrq     — 配置 MSI-X 并排空挂起事件
 *   XhciIrq           — 中断服务例程
 *   XhciDequeueKeyboard — 从软件队列取键盘报告
 */
/* PR-H-xhci-split-2：内部头；Diag 已迁 XhciDiag.c */
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
/* PR-H-msc-3：scan 临时 slot，独立 EP0，勿 InitRing 键盘 gEp0 */
UINT32 gMscScanSlot;
UINT8  gMscScanDevCtx[2048] __attribute__((aligned(64)));
XHCI_TRB gMscScanEp0Ring[RING_SIZE] __attribute__((aligned(64)));
RING_STATE gMscScanEp0;
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

/* 真机忙等，按 ~3GHz 估算。QEMU 不要用长 Stall。 */
void StallMs(UINT32 Ms) {
    UINT64 T0;
    UINT64 Need;

    if (Ms == 0) {
        return;
    }
    Need = (UINT64)Ms * 3000000ULL;
    T0 = ReadTsc();
    while (ReadTsc() - T0 < Need) {
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
void MousePush(void);
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



static int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
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

/* ---- PR-H-hub：一层 USB2 hub（根口 Class 9）---- */


int HubCtrl(UINT8 BmReq, UINT8 Req, UINT16 Value, UINT16 Index,
                   UINT16 Len, void *Data) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = BmReq,
        .bRequest = Req,
        .wValue = Value,
        .wIndex = Index,
        .wLength = Len
    };
    gXferSlot = gHubSlotId;
    return ControlXfer(&Setup, Data);
}

int FinishHubSetup(UINT8 *OutNumPorts) {
    UINT8 HubDesc[16];
    UINT8 Nports = 4;
    UINT8 ConfigVal = 1;

    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    ConfigVal = gCtrlBuf[5] ? gCtrlBuf[5] : 1;
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    ZeroMemory(HubDesc, sizeof(HubDesc));
    gHubTtt = 0;
    if (HubCtrl(0xA0, 0x06, 0x2900, 0, sizeof(HubDesc), HubDesc) == 0 &&
        HubDesc[2] != 0) {
        Nports = HubDesc[2];
        if (Nports > 15) {
            Nports = 15;
        }
        /* USB2 Hub Desc：wHubCharacteristics bit5-6 = TT Think Time */
        gHubTtt = (UINT8)((HubDesc[3] >> 5) & 3u);
    }
    if (OutNumPorts) {
        *OutNumPorts = Nports;
    }
    gHubNumPorts = Nports;
    BootLogV("boot: xhci hub ports ok\n");
    return 1;
}

static int HubGetPortStatus(UINT8 Port, UINT32 *OutSt) {
    UINT8 Buf[4];
    if (HubCtrl(0xA3, 0x00, 0, Port, 4, Buf) < 0) {
        return -1;
    }
    *OutSt = (UINT32)Buf[0] | ((UINT32)Buf[1] << 8) |
             ((UINT32)Buf[2] << 16) | ((UINT32)Buf[3] << 24);
    return 0;
}

static int HubSetPortFeat(UINT8 Port, UINT16 Feat) {
    return HubCtrl(0x23, 0x03, Feat, Port, 0, 0);
}

static int HubClearPortFeat(UINT8 Port, UINT16 Feat) {
    return HubCtrl(0x23, 0x01, Feat, Port, 0, 0);
}

/* hub 口速度：USB2 wPortStatus bits 10..9 → xHCI Port Speed 编码近似 */
static UINT8 HubPortSpeed(UINT32 St) {
    UINT32 Bits = (St >> 9) & 3u;
    if (Bits == 0) {
        return 1; /* full */
    }
    if (Bits == 1) {
        return 2; /* low */
    }
    if (Bits == 2) {
        return 3; /* high */
    }
    return 1;
}

int TryConfigureKeyboardSlot(UINT8 Speed) {
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;
    UINT16 Total;

    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps, &Interval);
    if (!HaveIntr) {
        EnumWhy("boot: why=no hid ep\n");
        return 0;
    }
    if (RealPcRejectMouseExtraAsKeyboard(Total, Speed)) {
        if (ClaimAddressedSlotAsMouse(gHubRootPort ? gHubRootPort : gPort1, Speed, Total,
                                      ConfigVal)) {
            /* gSlotId 已清；调用方 DisableSlot(0) 为空操作 */
            return 0;
        }
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    (void)SetProtocolBoot(gKbdIface);
    SetIdle(gKbdIface);
    {
        UINT8 MEp = 0, MIv = 10;
        UINT16 MMps = 8;
        int WantMouse;

        /*
         * 真机 v6：复合键鼠上 Add 鼠标后即便 Sync ok 仍 k=0。
         * 对照实验：只配键盘、不 Prep/Add 鼠标（避免 Stall 与二次 Config）。
         * 独立口鼠标仍可由 InitMouseOnPort 绑定。
         */
        WantMouse = HalCpuIsHypervisor() &&
                    PrepCompositeMouse(Total, Speed, gKbdIface, EpAddr, &MEp, &MMps, &MIv);
        if (!HalCpuIsHypervisor()) {
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                return 0;
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
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
            QueueMouseIntr();
            BootLog("boot: xhci-hid mouse (composite)\n");
        } else {
            if (!ConfigureIntr(EpAddr, Mps, Interval, Speed, 0, 0, 0)) {
                return 0;
            }
            ZeroMemory(gReportBuf, 8);
            QueueIntr();
        }
    }
    gUseGetReport = 0;
    return 1;
}

static int IsHubDeviceDesc(void) {
    /* GET_DESCRIPTOR device 已在 gCtrlBuf */
    if (gCtrlBuf[4] == 0x09) {
        return 1;
    }
    return 0;
}

/* 配置描述符中是否有 Hub Interface（bDeviceClass=0 的常见 hub） */
static int ConfigHasHubIface(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;

    while (Off + 9 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9 && Cfg[Off + 5] == 0x09) {
            return 1;
        }
        Off = (UINT16)(Off + Len);
    }
    return 0;
}

int EnumHubChildrenForKeyboard(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    volatile int D;

    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        int t;

        if (HubSetPortFeat(Port, HUB_FEAT_PORT_POWER) < 0) {
            continue;
        }
        if (!HalCpuIsHypervisor()) {
            StallMs(100);
        } else {
            for (D = 0; D < 80000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        BootLog("boot: xhci hub port connect\n");
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_RESET) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
            if (HubGetPortStatus(Port, &St) < 0) {
                break;
            }
            if (St & HUB_C_PORT_CONNECTION) {
                (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
            }
            if (St & HUB_PORT_ENABLE) {
                break;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(5);
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            continue;
        }
        Speed = HubPortSpeed(St);
        gSpeed = Speed;
        gPort1 = gHubRootPort;
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gSlotId, gDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            DisableSlot(gSlotId);
            continue;
        }
        if (GetDeviceDesc() < 0) {
            DisableSlot(gSlotId);
            continue;
        }
        if (IsHubDeviceDesc()) {
            /* 不做二层 hub */
            DisableSlot(gSlotId);
            continue;
        }
        if (!TryConfigureKeyboardSlot(Speed)) {
            DisableSlot(gSlotId);
            continue;
        }
        BootLog("boot: xhci-hid via hub\n");
        return 1;
    }
    return 0;
}

/* hub 子口找独立鼠标（根口 composite 弱 HID 被跳过时） */
int EnumHubChildrenForMouse(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;

    if (gHubSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        volatile int D;

        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
        if (!HalCpuIsHypervisor()) {
            StallMs(100);
        } else {
            for (D = 0; D < 80000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        BootLogV("boot: xhci hub mouse port\n");
        /* 跳过已占用为键盘的子口（同 route） */
        if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
            continue;
        }
        if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        {
            int t;
            for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_RESET) {
                    (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMs(5);
                }
            }
            /* 复位完成后须等 PORT_ENABLE，否则 Address 后中断 IN 永不完成 → m=0 */
            for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_CONNECTION) {
                    (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
                }
                if (St & HUB_PORT_ENABLE) {
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMs(5);
                }
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            BootLogV("boot: xhci hub mouse not PED\n");
            continue;
        }
        Speed = HubPortSpeed(St);
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gMouseSlotId, gMouseDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            gMouseSlotId = 0;
            continue;
        }
        if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            continue;
        }
        {
            UINT8 EpAddr = 0, Interval = 10, Iface = 0;
            UINT16 Mps = 8;
            UINT16 Total;
            if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
            if (Total < 9) {
                Total = 9;
            }
            if (Total > sizeof(gCtrlBuf)) {
                Total = (UINT16)sizeof(gCtrlBuf);
            }
            if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0 ||
                !ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            /*
             * 真机：拒绝弱 HID（score<2）。hub 上 U 盘/无线棒旁常有 vendor HID，
             * 误绑 → arms mouse=xx 但 PHOTO m=0；真鼠多在其它根口。
             */
            if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("boot: xhci hub skip weak mouse score=", gMouseParseScore, 2);
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            gMousePort = gHubRootPort;
            gMouseIface = Iface;
            BootLogHex("boot: xhci mouse hub ep=", EpAddr, 2);
            BootLogHex("boot: xhci mouse hub mps=", Mps, 2);
            BootLogHex("boot: xhci mouse hub iv=", Interval, 2);
            BootLogHex("boot: xhci mouse hub spd=", Speed, 1);
            BootLogHex("boot: xhci mouse hub score=", gMouseParseScore, 1);
            BootLogHex("boot: xhci mouse hub tt=",
                       ((UINT32)gMouseHubSlot << 8) | gMouseTtPort, 4);
            BootLogHex("boot: xhci mouse hub route=", gMouseRoute, 2);
            if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            {
                UINT32 *EpOut = (UINT32 *)(void *)(gMouseDevCtx + gCtxSize * gMouseIntrDci);
                FlushDma(EpOut, gCtxSize);
                BootLogHex("boot: xhci mouse epst=", EpOut[0] & 7u, 1);
            }
            ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
            QueueMouseIntr();
            BootLog("boot: xhci-hid mouse via hub\n");
            return 1;
        }
    }
    return 0;
}

/*
 * 认领根口 hub：SetConfig + hub desc；USB2 再 Evaluate Hub/MTT。
 * ExistingSlot：InitMouseOnPort 已 Address 的 hub，保留 slot 勿 Disable+重 Address
 * （重 Address 带 Hub 位常 cc=0x11；USB3 hub 亦不可设 Hub 位）。
 * 不碰 gSlotId（键盘已绑定时可安全认领另一根口上的 hub）。
 */
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot) {
    UINT8 Nports = 4;
    int Usb2Hub = (Speed < 4);

    if (gHubSlotId != 0) {
        if (ExistingSlot != 0 && ExistingSlot != gHubSlotId) {
            DisableSlot(ExistingSlot);
        }
        return 1;
    }
    BootLogV("boot: xhci claim hub\n");
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    /* Device Desc 多已在 gCtrlBuf；没有则补读再判 MTT */
    if (gCtrlBuf[4] != 0x09) {
        gXferSlot = ExistingSlot ? ExistingSlot : 0;
        if (ExistingSlot != 0) {
            gEp0Mps = SpeedMps(Speed);
            (void)GetDeviceDesc();
        }
    }
    HubNoteMttFromDevDesc(Speed);

    if (ExistingSlot != 0) {
        BootLogV("boot: xhci hub adopt slot\n");
        gHubSlotId = ExistingSlot;
        gXferSlot = ExistingSlot;
        gEp0Mps = SpeedMps(Speed);
        /* 该 slot 先前按 mouse 环 Address；迁到 hub 专用环，避免后续鼠 Address 踩坏 TT */
        RecoverEp0(gHubSlotId);
        if (!FinishHubSetup(&Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("boot: why=hub cfg\n");
            return 0;
        }
        if (Usb2Hub && !EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
            DisableSlot(gHubSlotId);
            gHubSlotId = 0;
            EnumWhy("boot: why=hub eval\n");
            return 0;
        }
        BootLogHex("boot: xhci hub spd=", Speed, 1);
        BootLog("boot: xhci ep0=split\n");
        return 1;
    }

    gEp0Mps = SpeedMps(Speed);
    if (!AddressDeviceOnPort(RootPort, Speed, &gHubSlotId, gHubDevCtx,
                             0, 0, 0, Usb2Hub ? 1 : 0, Usb2Hub ? 4 : 0)) {
        gHubSlotId = 0;
        EnumWhy("boot: why=hub addr\n");
        return 0;
    }
    /* Address 后才有 Device Desc → 再定 MTT，随后 Evaluate 写入 */
    if (GetDeviceDesc() == 0) {
        HubNoteMttFromDevDesc(Speed);
    }
    if (!FinishHubSetup(&Nports)) {
        DisableSlot(gHubSlotId);
        gHubSlotId = 0;
        return 0;
    }
    if (Usb2Hub && !EvaluateHubSlot(gHubSlotId, RootPort, Speed, Nports)) {
        BootLog("boot: xhci hub eval skip\n");
    }
    return 1;
}

/* 根口已 Address：device class=9 或配置含 hub iface → 枚举子口找键盘 */
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed) {
    BootLog("boot: xhci hub on root\n");
    /* 重新 Address 为 Hub 设备（带 Hub 位）；此时 gSlotId 是误 Address 的非 hub */
    DisableSlot(gSlotId);
    gSlotId = 0;
    if (!ClaimHubOnRootPort(RootPort, Speed, 0)) {
        return 0;
    }
    if (EnumHubChildrenForKeyboard()) {
        return 1;
    }
    return 0;
}


/*
 * 真机常见：USB 键鼠复合设备（同一 slot 上键盘 Proto=1 + 鼠标 Proto=2）。
 * 旧逻辑只扫「其它根口」，同口第二接口永远绑不上 → arms mouse=00/00。
 */
int InitMouseOnKeyboardSlot(void) {
    UINT8 EpAddr = 0, Interval = 10, Iface = 0;
    UINT16 Mps = 8;
    UINT16 Total;
    UINT8 CurAlt = 0;
    UINT8 BestAlt = 0;
    UINT16 Off;

    if (gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }

    gXferSlot = gSlotId;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, gSpeed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    /*
     * 真机：拒绝弱评分（多为键盘上的 media/vendor HID，proto=0 且永不报指针）。
     * QEMU tablet 走独立口 InitMouseOnPort，不受此限。
     */
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        BootLogHexV("boot: xhci skip weak composite score=", gMouseParseScore, 2);
        return 0;
    }
    if (Iface == gKbdIface) {
        return 0;
    }
    if ((EpAddr & 0x0F) == (gKbdEpAddr & 0x0F) && ((EpAddr ^ gKbdEpAddr) & 0x80) == 0) {
        return 0;
    }

    /* 找回该 iface 的 bAlternateSetting（Parse 未导出） */
    Off = 0;
    while (Off + 9 <= Total) {
        UINT8 Len = gCtrlBuf[Off];
        UINT8 Type = gCtrlBuf[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurAlt = gCtrlBuf[Off + 3];
            if (gCtrlBuf[Off + 2] == Iface && gCtrlBuf[Off + 5] == 3 &&
                gCtrlBuf[Off + 7] != 1) {
                BestAlt = CurAlt;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    gMousePort = gPort1;
    gMouseIface = Iface;
    (void)SetInterface(Iface, BestAlt);
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    (void)SetIdle(Iface);

    BootLogHexV("boot: xhci mouse iface=", Iface, 2);
    BootLogHexV("boot: xhci mouse proto=", gMouseIfaceProto, 2);
    BootLogHexV("boot: xhci mouse ep=", EpAddr, 2);
    BootLogHexV("boot: xhci mouse mps=", Mps, 2);
    /* 单行汇总：串口好抄 */
    {
        char Line[72];
        char Hex[12];
        int n = 0;
        const char *P = "boot: xhci mouse cfg i=";
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Iface, 2);
        P = Hex;
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        P = " p=";
        while (*P && n < 44) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gMouseIfaceProto, 2);
        P = Hex;
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        P = " ep=";
        while (*P && n < 54) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EpAddr, 2);
        P = Hex;
        while (*P && n < 58) {
            Line[n++] = *P++;
        }
        P = " mps=";
        while (*P && n < 64) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Mps, 2);
        P = Hex;
        while (*P && n < 68) {
            Line[n++] = *P++;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line); /* 真机 BootMark → COM1 + 屏 */
    }

    {
        int MouseCfg;

        MouseCfg = ConfigureMouseIntr(gSlotId, EpAddr, Mps, Interval, gSpeed);
        if (!MouseCfg) {
            BootLog("boot: xhci composite mouse ep fail\n");
            gMouseIntrDci = 0;
            gMouseEpAddr = 0;
            return 0;
        }

        gMouseSlotId = gSlotId;
        ZeroMemory(gReportBuf, 8);
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        /*
         * MouseCfg：1=add-only 键盘未停 → 勿 Sync；2/3=曾 Stop/Drop → Sync 键盘。
         * Arm 同样勿再 Sync（PHOTO 上 Sync 后 k 仍 0）。
         */
        if (MouseCfg >= 2 && gSlotId != 0 && gIntrDci != 0) {
            if (SyncIntrDequeue(gSlotId, gIntrDci, gIntrRing, &gIntr, sizeof(gIntrRing)) == 0) {
                BootLog("boot: xhci sync kbd after mouse\n");
            } else {
                BootLog("boot: xhci sync kbd after mouse fail\n");
            }
            QueueIntr();
        }
        QueueMouseIntr();
        BootLog("boot: xhci composite kbd rearm\n");
    }
    {
        char Line[80];
        int n = 0;
        const char *P = "boot: xhci kbd iface=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gKbdIface / 10) % 10));
        Line[n++] = (char)('0' + (gKbdIface % 10));
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        P = " mouse dci=";
        while (*P && n < 56) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gMouseIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gMouseIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    if (gMouseAbsolute) {
        BootLog("boot: xhci-hid mouse (composite abs)\n");
    } else {
        BootLog("boot: xhci-hid mouse (composite)\n");
    }
    return 1;
}

int InitMouseOnPort(UINT32 Port1) {
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port1));
    UINT16 Total;
    UINT32 WasSlot;
    int Force;
    UINT8 Speed;
    UINT8 DevClass;

    if (gPortNoHid & (1u << Port1)) {
        BootLogHexV("boot: xhci mouse skip port=", Port1, 2);
        return 0;
    }
    BootLogHexV("boot: xhci mouse try port=", Port1, 2);
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    /*
     * 仅对「本轮已 Address 再 Disable」的口强制 PR（否则 cc=0x04）。
     * 全口 Force PR + 长超时会空转很久，短按电源失效。
     */
    Force = (gPortNeedForcePr & (1u << Port1)) ? 1 : 0;
    if (!ResetPortEx(Port1, Force)) {
        return 0;
    }
    if (Force && !HalCpuIsHypervisor()) {
        StallMs(20);
    }
    Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
    gMousePort = Port1;
    gMouseRoute = 0;
    gMouseHubSlot = 0;
    gMouseTtPort = 0;
    gMouseAbsolute = 0;
    gMouseIfaceProto = 0;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
        gMouseSlotId = 0;
        /* 未强制过且 Address 失败：再 Force PR 试一次（真鼠口常见） */
        if (!Force && (gCmdCode == 4 || gCmdCode == 0x11)) {
            if (!ResetPortEx(Port1, 1)) {
                return 0;
            }
            if (!HalCpuIsHypervisor()) {
                StallMs(20);
            }
            Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
            if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
                gMouseSlotId = 0;
                return 0;
            }
        } else {
            return 0;
        }
    }

    /*
     * 键盘已绑定时，其它根口上的 hub 不会再走 TryHubOnRootPort。
     * 勿把 hub 当 HID（SetConfig/SetIdle → Stall cc=6）；认领 hub 后扫子口鼠标。
     */
    gXferSlot = gMouseSlotId;
    if (GetDeviceDesc() < 0) {
        gPortNeedForcePr |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    DevClass = gCtrlBuf[4];
    /* Mass Storage / Wireless：非鼠标，快跳过，避免 SetupHid 超时拖死启动 */
    if (DevClass == 0x08 || DevClass == 0xE0) {
        BootLogHex("boot: xhci mouse skip class=", DevClass, 2);
        gPortNoHid |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (IsHubDeviceDesc()) {
        WasSlot = gMouseSlotId;
        gMouseSlotId = 0;
        BootLogV("boot: xhci mouse-scan hub (class 9)\n");
        if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
            return EnumHubChildrenForMouse();
        }
        return 0;
    }
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) == 0) {
        Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
        if (Total < 9) {
            Total = 9;
        }
        if (Total > sizeof(gCtrlBuf)) {
            Total = (UINT16)sizeof(gCtrlBuf);
        }
        /*
         * 完整配置描述符：真机部分设备大包会超时；失败则 RecoverEp0 后仍走
         * SetupHidDevice（其内部会再取描述符）。勿在 EP0 失步时直接放弃。
         */
        if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
            BootLog("boot: xhci mouse cfg desc retry\n");
            RecoverEp0(gMouseSlotId);
            gXferSlot = gMouseSlotId;
        } else if (ConfigHasHubIface(gCtrlBuf, Total)) {
            WasSlot = gMouseSlotId;
            gMouseSlotId = 0;
            BootLog("boot: xhci mouse-scan hub (iface 9)\n");
            if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
                return EnumHubChildrenForMouse();
            }
            return 0;
        }
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
        DebugWrite("XHCI: mouse config failed\n");
        /* 再 Force PR + 重 Address 一次（port4 真鼠曾卡在 cfg） */
        if (!HalCpuIsHypervisor()) {
            UINT32 Old = gMouseSlotId;
            BootLogV("boot: xhci mouse root retry\n");
            DisableSlot(Old);
            gMouseSlotId = 0;
            if (ResetPortEx(Port1, 1)) {
                if (!HalCpuIsHypervisor()) {
                    StallMs(20);
                }
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
                if (AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx,
                                        0, 0, 0, 0, 0) &&
                    SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
                    /* fall through to EP setup below */
                } else {
                    if (gMouseSlotId) {
                        DisableSlot(gMouseSlotId);
                    }
                    gMouseSlotId = 0;
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            gPortNeedForcePr |= (1u << Port1);
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }

    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 Iface = 0;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCtrlBuf)) {
        Total = (UINT16)sizeof(gCtrlBuf);
    }
    if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
        RecoverEp0(gMouseSlotId);
        gXferSlot = gMouseSlotId;
        if (GetDesc(0x0200, 0, Total, gCtrlBuf) < 0) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        DebugWrite("XHCI: mouse no interrupt EP\n");
        BootLog("boot: xhci skip non-mouse HID\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("boot: xhci skip weak root mouse score=", gMouseParseScore, 2);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    gMouseIface = Iface;
    BootLogHexV("boot: xhci mouse root score=", gMouseParseScore, 2);
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    DebugWrite("XHCI: mouse ready\n");
    if (gMouseAbsolute) {
        BootLog("boot: xhci-hid mouse (abs)\n");
    } else {
        BootLog("boot: xhci-hid mouse\n");
    }
    return 1;
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

/* 真机 PHOTO 后再绑鼠标，避免复合/hub 扫描踩键盘 EP */
void XhciInitMouseDeferred(void) {
    if (HalCpuIsHypervisor()) {
        return;
    }
    if (gMouseSlotId != 0) {
        return;
    }
    ToyLogUsb("boot: xhci mouse deferred start\n");
    if (gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId == 0) {
        for (UINT32 p = 1; p <= gMaxPorts; p++) {
            if (gSlotId != 0 && p == gPort1) {
                continue;
            }
            if (InitMouseOnPort(p)) {
                break;
            }
        }
    }
    if (gMouseSlotId == 0) {
        (void)InitMouseOnKeyboardSlot(); /* 回退：保鼠标 */
    }
    if (gMouseSlotId == 0 && gHubSlotId != 0) {
        (void)EnumHubChildrenForMouse();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0) {
        ToyLogUsb("boot: xhci mouse deferred ok\n");
    } else {
        ToyLogUsb("boot: xhci mouse deferred none\n");
    }
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

void MousePush(void) {
    UINT32 Next = (gMouseWriteIndex + 1) % MOUSE_Q;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    int UseAbsolute;
    UINT8 ParseLen;

    if (Next == gMouseReadIndex) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseWriteIndex];
    R->Wheel = 0;
    R->Absolute = 0;
    ParseLen = gMouseXferLen ? gMouseXferLen : gMouseReportLen;
    if (ParseLen < 3) {
        ParseLen = 3;
    }
    if (ParseLen > 8) {
        ParseLen = 8;
    }
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    /*
     * 仅枚举标了 gMouseAbsolute（QEMU tablet）才走绝对。
     * boot 相对鼠（Proto=2）禁止「高字节启发式」——短包残留曾把 dx/dy
     * 当成 16-bit 绝对坐标，Gui 再 /32767 → 光标钉死在角上（PHOTO m 涨、桌面不动）。
     */
    UseAbsolute = 0;
    if (gMouseAbsolute && gMouseIfaceProto != 2) {
        if (ParseLen >= 5 && X0 <= 32767 && Y0 <= 32767) {
            UseAbsolute = 1;
        } else if (ParseLen >= 6 && gMouseBuf[0] != 0 && X1 <= 32767 &&
                   Y1 <= 32767) {
            UseAbsolute = 2;
        }
    }

    if (UseAbsolute == 1) {
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
        R->Absolute = 1;
        if (ParseLen >= 6) {
            R->Wheel = (INT8)gMouseBuf[5];
        }
    } else if (UseAbsolute == 2) {
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
        R->Absolute = 1;
        if (ParseLen >= 7) {
            R->Wheel = (INT8)gMouseBuf[6];
        }
    } else {
        /* HID boot 相对鼠标：b0 buttons, b1 X, b2 Y, b3 wheel */
        int Dx = (int)(signed char)gMouseBuf[1];
        int Dy = (int)(signed char)gMouseBuf[2];
        if (!gMouseAbsInit) {
            gMouseAbsInit = 1;
        }
        gMouseAbsX += Dx;
        gMouseAbsY += Dy;
        if (gMouseAbsX < 0) {
            gMouseAbsX = 0;
        }
        if (gMouseAbsY < 0) {
            gMouseAbsY = 0;
        }
        if (gMouseAbsX > 3840) {
            gMouseAbsX = 3840;
        }
        if (gMouseAbsY > 2160) {
            gMouseAbsY = 2160;
        }
        R->X = (UINT32)gMouseAbsX;
        R->Y = (UINT32)gMouseAbsY;
        R->Buttons = gMouseBuf[0] & 7;
        if (ParseLen >= 4) {
            R->Wheel = (INT8)gMouseBuf[3];
        }
    }
    gMouseWriteIndex = Next;
}

/* 清除中断管理器挂起位 */
void ImClearPending(void) {
    UINT32 Im = ReadMmio32(gRuntimeBase + 0x20);
    WriteMmio32(gRuntimeBase + 0x20, Im | 1u);
}

/* XHCI MSI-X/MSI 中断：事件处理；真机走 RealPc 路径（粘 EINT/CCS） */
void XhciIrq(void) {
    int RealPc = !HalCpuIsHypervisor();

    gStatIrq++;
    SpinLockAcquire(&gHidQueueLock);
    if (RealPc) {
        ProcessEventsRealPc();
    } else {
        ProcessEvents();
    }
    if (gIntrDone) {
        gIntrDone = 0;
        if (gIntrReportReady) {
            gIntrReportReady = 0;
            FlushDma(gReportBuf, sizeof(gReportBuf));
            KbdPush();
            gStatKbdPush++;
        }
        QueueIntr();
    }
    if (gMouseIntrDone) {
        gMouseIntrDone = 0;
        if (gMouseReportReady) {
            gMouseReportReady = 0;
            FlushDma(gMouseBuf, sizeof(gMouseBuf));
            MousePush();
            gStatMousePush++;
        }
        QueueMouseIntr();
    }
    if (gRuntimeBase != 0) {
        ImClearPending();
    }
    SpinLockRelease(&gHidQueueLock);
}

/* 开 USBCMD.INTE + IMAN.IE（真机 Start 故意只置了 RS） */
void EnableHostInterrupts(void) {
    UINT32 Cmd;

    if (gOperationalBase != 0) {
        Cmd = ReadMmio32(gOperationalBase);
        WriteMmio32(gOperationalBase, Cmd | USBCMD_RS | USBCMD_INTE);
    }
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 3u); /* IE | IP(W1C) */
    }
}

/*
 * 排空事件环。
 * POLL / DUAL：盲 ProcessEvents×32（backup；真机 dual 时 q=0 也靠这条活）。
 * IRQ（PR-H-xhci-irq）：减为 ×1 + 门铃轻推；长时间无新 IRQ → FallbackToPoll。
 */
void XhciDrainEvents(void) {
    int i;
    int RealPc = !HalCpuIsHypervisor();
    int Passes = 32;
    static UINT32 sLastIrq;
    static UINT32 sIrqStall;

    gStatDrain++;

    /* PR-H-xhci-irq：有 IRQ 证据才维持轻量 Drain；停滞则回 poll */
    if (gIrqMode == XHCI_IRQ_MODE_IRQ) {
        if (gStatIrq != sLastIrq) {
            sLastIrq = gStatIrq;
            sIrqStall = 0;
        } else if (++sIrqStall > 200000u) {
            sIrqStall = 0;
            XhciFallbackToPoll("irq-stall");
            /* Fallback 已 Drain；下面按 POLL 再走一轮无妨 */
        } else {
            Passes = 1; /* 减 poll */
        }
    } else if (gIrqMode == XHCI_IRQ_MODE_DUAL) {
        /* q 涨起来后再升 IRQ（懒升；真机 q=0 永留 dual） */
        if (gStatIrq >= 3u && gUseIrq) {
            gIrqMode = XHCI_IRQ_MODE_IRQ;
            sLastIrq = gStatIrq;
            sIrqStall = 0;
            BootLog("boot: xhci irq=msi (irq)\n");
            Passes = 1;
        }
    }

    SpinLockAcquire(&gHidQueueLock);
    for (i = 0; i < Passes; i++) {
        if (RealPc) {
            ProcessEventsRealPc();
        } else {
            ProcessEvents();
        }
        if (gIntrDone) {
            gIntrDone = 0;
            if (gIntrReportReady) {
                gIntrReportReady = 0;
                FlushDma(gReportBuf, sizeof(gReportBuf));
                KbdPush();
                gStatKbdPush++;
            }
            QueueIntr();
        }
        if (gMouseIntrDone) {
            gMouseIntrDone = 0;
            if (gMouseReportReady) {
                gMouseReportReady = 0;
                FlushDma(gMouseBuf, sizeof(gMouseBuf));
                MousePush();
                gStatMousePush++;
            }
            QueueMouseIntr();
        }
    }
    /*
     * 禁止在持 gHidQueueLock 时 HidGetInputReport/ControlXfer：
     * WaitCommand 可达数百 ms 且 IF=1，定时器切到 Gui 再 Drain → 同锁死锁，
     * 家侧表现为桌面不能打字、短按电源无效（须长按强制关机）。
     * PHOTO 已证明中断 IN 可完成；门铃轻推即可，勿走 EP0 兜底。
     */
    if (RealPc) {
        if (gSlotId != 0 && gIntrDci != 0 && (gStatDrain & 0xFu) == 0) {
            RingDoorbell(gSlotId, gIntrDci);
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0 && (gStatDrain & 0x7u) == 0) {
            RingDoorbell(gMouseSlotId, gMouseIntrDci);
        }
    }
    if (gUseIrq && gRuntimeBase != 0) {
        ImClearPending();
    }
    SpinLockRelease(&gHidQueueLock);
    /* 释锁后再 GET_REPORT，避免与 WaitTransfer 嵌套抢同一把锁 */
    if (RealPc) {
        XhciPollKbdGetReport();
    }
}

/* dual/irq → 切回 poll 备份（关 host IE；不拆 PCI MSI 表亦可，避免半残状态） */
void XhciFallbackToPoll(const char *Why) {
    UINT32 Cmd;
    char Line[72];
    int n = 0;
    const char *P = "boot: xhci irq=poll (fallback)";
    const char *W = Why;

    gUseIrq = 0;
    gIrqMode = XHCI_IRQ_MODE_POLL;
    if (gOperationalBase != 0) {
        Cmd = ReadMmio32(gOperationalBase);
        WriteMmio32(gOperationalBase, (Cmd | USBCMD_RS) & ~USBCMD_INTE);
    }
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 0); /* clear IE */
    }
    while (*P && n + 1 < (int)sizeof(Line)) {
        Line[n++] = *P++;
    }
    if (W && W[0] && n + 2 < (int)sizeof(Line)) {
        Line[n++] = ' ';
        while (*W && n + 1 < (int)sizeof(Line)) {
            Line[n++] = *W++;
        }
    }
    if (n + 1 < (int)sizeof(Line)) {
        Line[n++] = '\n';
    }
    Line[n] = 0;
    BootLog(Line); /* 真机 PHOTO 可见；勿只用 ToyLogUsb */
    XhciDrainEvents();
}

/*
 * PR-H-xhci-dual：试 MSI-X/MSI + host IE → DUAL。
 * Drain 在 DUAL 下仍盲排空（XhciDrainEvents 不看 gUseIrq 关排空）。
 * 失败由调用方 XhciFallbackToPoll。
 */
int XhciTryEnterDual(USB_CONTROLLER *Device) {
    if (gIrqMode == XHCI_IRQ_MODE_DUAL && gUseIrq) {
        return 1;
    }
    if (!Device) {
        return 0;
    }
    if (!PciEnableMsi(Device, VEC_XHCI)) {
        return 0;
    }
    EnableHostInterrupts();
    /* 先盲排空再开 gUseIrq，避免半开窗口丢完成 */
    gUseIrq = 0;
    XhciDrainEvents();
    gUseIrq = 1;
    gIrqMode = XHCI_IRQ_MODE_DUAL;
    BootLog("boot: xhci irq=msi (dual)\n"); /* PHOTO ring 可抄 */
    return 1;
}

XHCI_IRQ_MODE XhciIrqMode(void) {
    return gIrqMode;
}

/* PHOTO/Shell：mode=poll|dual|irq + t/i/k/m/u/s/c/r/d/q（q=IRQ 进入次数） */

/* Arm 后打一枪：期望的键鼠 slot/DCI，便于对照 s=. */

/*
 * QEMU：MSI/IOAPIC → DUAL。真机：试 dual；失败 → poll (fallback)，Drain 永不关。
 */
int XhciEnableIrq(USB_CONTROLLER *Device) {
    UINT8 Dest;

    if (gUseGetReport || (gSlotId == 0 && gMouseSlotId == 0)) {
        DebugWrite("XHCI: no interrupt EP, IRQ unused\n");
        gUseIrq = 0;
        gIrqMode = XHCI_IRQ_MODE_POLL;
        ToyLogUsb("boot: xhci irq=none\n");
        return 0;
    }

    /* PHOTO / show xhci：Arm 后计数清零 */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKbdPush = 0;
    gStatMousePush = 0;
    gStatXferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEp = 0;
    gStatIrq = 0;
    gDiagXferLogged = 0;
    gDiagIntrCcLogged = 0;
    XhciDiagLogArms();

    /*
     * 真机 Arm：只 Queue，勿 Sync（Stop+SetDeq 曾弄死 kbd / PHOTO r=0）。
     */
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    /* Queue 产生的 Stopped 勿计入 PHOTO */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKbdPush = 0;
    gStatMousePush = 0;
    gStatXferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEp = 0;

    if (XhciTryEnterDual(Device)) {
        XhciDrainEvents();
        return 1;
    }

    /* QEMU：再试 INTx→IOAPIC；真机 dual 失败则纯 poll backup */
    if (HalCpuIsHypervisor()) {
        Dest = HalCpuApicId(0);
        if (PciEnableIoApicIntx(Device, VEC_XHCI, Dest)) {
            EnableHostInterrupts();
            gUseIrq = 0;
            XhciDrainEvents();
            gUseIrq = 1;
            gIrqMode = XHCI_IRQ_MODE_DUAL;
            ToyLogUsb("boot: xhci irq=ioapic (dual)\n");
            return 1;
        }
    }

    DebugWrite("XHCI: MSI failed; poll drain backup\n");
    XhciFallbackToPoll("no-msi");
    return 0;
}

/* 返回是否武装了设备中断（DUAL/IRQ）；POLL 时仍靠 Drain */
int XhciUsesIrq(void) {
    return gUseIrq != 0;
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

int XhciMousePresent(void) {
    return gMouseSlotId != 0;
}

/*
 * PHOTO 里 HalInputPoll 只 Push 不消费 → 鼠队列易满。
 * 进桌面前只抽空队列并对齐 Abs；勿 SyncIntrDequeue（枚举后多余 Stop 曾致 PHOTO r=0）。
 */
void XhciMouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    if (HalCpuIsHypervisor()) {
        return;
    }
    SpinLockAcquire(&gHidQueueLock);
    gMouseReadIndex = gMouseWriteIndex;
    gMouseAbsX = (int)CursorX;
    gMouseAbsY = (int)CursorY;
    gMouseAbsInit = 1;
    if (gMouseAbsX < 0) {
        gMouseAbsX = 0;
    }
    if (gMouseAbsY < 0) {
        gMouseAbsY = 0;
    }
    SpinLockRelease(&gHidQueueLock);
    ToyLogUsb("boot: xhci mouse handoff desktop\n");
}

int XhciDequeueMouse(USB_MOUSE_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gHidQueueLock);
    if (gMouseReadIndex != gMouseWriteIndex) {
        *Report = gMouseQ[gMouseReadIndex];
        gMouseReadIndex = (gMouseReadIndex + 1) % MOUSE_Q;
        Ok = 1;
    }
    SpinLockRelease(&gHidQueueLock);
    return Ok;
}

/*
 * PR-H-msc-2：证明 Bulk 环可 Init；故意不扫口 / Reset / Address / Control。
 * 后续 msc-3+ 再接枚举与 BOT。
 */
int XhciMscBringUp(void) {
    if (!gMscBulkRingsInited) {
        InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
        InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);
        FlushDma(gBulkInRing, sizeof(gBulkInRing));
        FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
        gMscBulkRingsInited = 1;
    }
    return -1;
}

int XhciMscReady(void) {
    return 0;
}

int XhciBulkXfer(int DirIn, void *Buf, UINT32 Len) {
    (void)DirIn;
    (void)Buf;
    (void)Len;
    return -1;
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
    if (gMscScanSlot != 0) {
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
        if (!(Ps & PORTSC_PED)) {
            /* 故意不 Force PR：留给 msc-4+ */
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

        /*
         * bDeviceClass=0：看配置里第一个 Interface Class（只 GetDesc，不 SetConfig）。
         */
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

        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }

    BootLogHex("boot: msc scan done n=", (UINT32)Found, 2);
    return Found;
}
