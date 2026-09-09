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
#include "XHCI.h"
#include "Console.h"
#include "Hal.h"
#include "Debug.h"
#include "AcpiMadt.h"
#include "Platform.h"
#include "SpinLock.h"
#include "VirtualMemory.h"

#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#endif
/* 固件 DMA 页：UC，避免 CPU cache 挡住 HC 读 TRB / 写事件 */
#define PTE_XHCI_DMA (PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD)

#define RING_SIZE           32
#define EVT_SIZE            32
#define DCBAA_SLOTS         16
#define PORTSC_CCS          (1u << 0)
#define PORTSC_PED          (1u << 1)
#define PORTSC_OCA          (1u << 3)
#define PORTSC_PR           (1u << 4)
#define PORTSC_PP           (1u << 9)
#define PORTSC_SPEED_SHIFT  10
#define PORTSC_CSC          (1u << 17)
#define PORTSC_PEC          (1u << 18)
#define PORTSC_WRC          (1u << 19)
#define PORTSC_OCC          (1u << 20)
#define PORTSC_PRC          (1u << 21)
#define PORTSC_PLC          (1u << 22)
#define PORTSC_CEC          (1u << 23)
#define PORTSC_WPR          (1u << 31)
/* 写 PORTSC 时保留的 RO / 状态位（对齐 Linux xhci_port_state_to_neutral） */
#define PORTSC_RO           (PORTSC_CCS | PORTSC_OCA | (0xFu << PORTSC_SPEED_SHIFT) | (1u << 30))
#define PORTSC_RWS          (PORTSC_PED | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | \
                             PORTSC_PP | (1u << 14) | (1u << 15) | (1u << 16) | \
                             (0x1Fu << 24) | PORTSC_WPR)
#define PORTSC_CHANGE       (PORTSC_CSC | PORTSC_PEC | PORTSC_WRC | PORTSC_OCC | \
                             PORTSC_PRC | PORTSC_PLC | PORTSC_CEC)

#define USBCMD_RS           (1u << 0)
#define USBCMD_HCRST        (1u << 1)
#define USBCMD_INTE         (1u << 2)
#define USBSTS_HCH          (1u << 0)
#define USBSTS_HSE          (1u << 1)
#define USBSTS_EINT         (1u << 2)
#define USBSTS_CNR          (1u << 6)
#define CRCR_CA             (1u << 2)
#define CRCR_CRR            (1u << 3)
#define XHCI_FW_CMD_SIZE    256
#define XHCI_FW_EVT_MAX     256

#define TRB_C               (1u << 0)
#define TRB_TC              (1u << 1)
#define TRB_IOC             (1u << 5)
#define TRB_IDT             (1u << 6)
#define TRB_TYPE(t)         ((UINT32)(t) << 10)
#define TRB_SLOT(s)         ((UINT32)(s) << 24)
#define TRB_TRT_OUT         (2u << 16)
#define TRB_TRT_IN          (3u << 16)
#define TRB_DIR_IN          (1u << 16)

#define TRB_NORMAL          1
#define TRB_SETUP           2
#define TRB_DATA            3
#define TRB_STATUS          4
#define TRB_LINK            6
#define TRB_ENABLE_SLOT     9
#define TRB_DISABLE_SLOT   10
#define TRB_ADDRESS_DEV    11
#define TRB_CONFIG_EP       12
#define TRB_EVALUATE_CTX   13
#define TRB_RESET_EP       14
#define TRB_STOP_EP        15
#define TRB_SET_TR_DEQ     16
#define TRB_TRANSFER_EVENT 32
#define TRB_CMD_COMPLETION  33

#define CC_SUCCESS          1
#define CC_SHORT_PACKET     13
#define CC_CONTEXT_STATE    19 /* SetTrDeq 常见：EP 状态不允许 */
#define CC_STOPPED          26 /* Stop EP 取消挂起传输 */
#define CC_STOPPED_LEN      27
#define CC_STOPPED_SHORT    28

typedef struct {
    UINT64 Parameter;
    UINT32 Status;
    UINT32 Control;
} __attribute__((packed, aligned(16))) XHCI_TRB;

typedef struct {
    UINT32 Enq;
    UINT32 Pcs;
    UINT32 Size; /* TRB 个数（含末尾 LINK） */
} RING_STATE;

static UINT64 gCapabilityBase;
static UINT64 gOperationalBase;
static UINT64 gDoorbellBase;
static UINT64 gRuntimeBase;
static UINT32 gCtxSize;
static UINT32 gMaxPorts;
/* 真机探针：DMAR/TE 留给写 RS 前那行黄字 */
static int gXhciDmar = -2; /* -2未查 -1坏 0无 1有 */
static int gXhciTe = -2;   /* -2未做 -1失败 0无DRHD 1本关 2已关 */
static UINT32 gPort1;
static UINT8  gSpeed;
static UINT32 gSlotId;
static UINT32 gXferSlot;
static UINT32 gIntrDci;
static UINT16 gEp0Mps;
static UINT8  gKbdIface;
static UINT8  gKbdEpAddr; /* 配置描述符 bEndpointAddress，匹配事件用 */
static UINT8  gUseGetReport;
static UINT8  gUseIrq;
/* 真机默认 POLL；DUAL/IRQ 见 XhciTryEnterDual（占位） */
static XHCI_IRQ_MODE gIrqMode = XHCI_IRQ_MODE_POLL;
/* 键盘 Slot 的路由/TT，Configure Endpoint 必须带回，否则 hub 子设备 cfg 失败 */
static UINT32 gKbdRoute;
static UINT8  gKbdHubSlot;
static UINT8  gKbdTtPort;
static const char *gEnumWhy;

#define KBD_Q 16
static USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
static volatile UINT32 gKeyboardWriteIndex;
static volatile UINT32 gKeyboardReadIndex;

static UINT32 gMouseSlotId;
static UINT32 gMousePort;
static UINT32 gMouseIntrDci;
static UINT8  gMouseIface;
static UINT8  gMouseEpAddr;
static UINT8  gMouseReportLen;
static UINT8  gMouseBuf[8] __attribute__((aligned(64)));
static XHCI_TRB gMouseIntrRing[RING_SIZE] __attribute__((aligned(64)));
static RING_STATE gMouseIntr;
static UINT8  gMouseDevCtx[2048] __attribute__((aligned(64)));
static volatile UINT32 gMouseIntrDone;
static volatile UINT32 gIntrReportReady;
static volatile UINT32 gMouseReportReady;
/* poll 诊断：PHOTO/桌面可看完成与推送是否在涨 */
static volatile UINT32 gStatIntrEvt;
static volatile UINT32 gStatMouseEvt;
static volatile UINT32 gStatKbdPush;
static volatile UINT32 gStatMousePush;
static volatile UINT32 gStatLastCc;
static volatile UINT32 gStatDrain;
static volatile UINT32 gStatXferAny;   /* 任意 Transfer Event */
static volatile UINT32 gStatEvtRing;   /* 事件环弹出次数（含命令完成） */
static volatile UINT32 gStatLastSlot;
static volatile UINT32 gStatLastEp;
static volatile UINT32 gStatUnmatched; /* Transfer 且未匹配键鼠 DCI */
static UINT32 gDiagXferLogged;        /* 限制串口/屏日志条数 */
static UINT32 gDiagQuiet;             /* GET_REPORT poll：勿 DiagChk 刷屏/盖白字 */
static UINT32 gDiagIntrCcLogged;

#define MOUSE_Q 32
static USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
static volatile UINT32 gMouseWriteIndex;
static volatile UINT32 gMouseReadIndex;
static SPIN_LOCK gHidQueueLock; /* PR-S-ap：IRQ 入队 vs AP 出队 */

static XHCI_TRB gCmdRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEp0Ring[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gIntrRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEvtRing[EVT_SIZE] __attribute__((aligned(64)));
/* 真机可指向固件环（IOMMU 已映射）；QEMU 用上面静态缓冲 */
static XHCI_TRB *gCmdRingLive = gCmdRing;
static XHCI_TRB *gEvtRingLive = gEvtRing;
static UINT32 gEvtRingSize = EVT_SIZE;

static RING_STATE gCmd;
static RING_STATE gEp0;
static RING_STATE gIntr;
static UINT32 gEvtDeq;
static UINT32 gEvtCcs;

static UINT64 gDcbaa[DCBAA_SLOTS + 1] __attribute__((aligned(64)));
/*
 * 真机 bRS 2：自建 DCBAA/scratch 后 RS 挂；固件 DCBAAP 可 RS。
 * gDcbaaLive 指向固件表或本地 gDcbaa；槽位写入走 DcbaaSet。
 */
static UINT64 *gDcbaaLive;
static UINT32 gDcbaaMaxSlot;
static int gDcbaaFromFirmware;
/*
 * 真机原则：固件已提供的 DMA 结构（DCBAAP/scratch、CRCR、ERST/事件环）优先沿用；
 * 禁止默认改指到内核 .bss。Halt 前快照；仅快照全空时才在固件 DCBAA 同页内切环。
 */
static UINT64 gFwDcbaapSave;
static UINT64 gFwCrcrSave;   /* CRCR 指针（已清低 6 位）= 当前 dequeue，非必然环基址 */
static UINT32 gFwCrcrRcs;    /* CRCR.RCS，与 dequeue 配对 */
static UINT64 gFwErstbaSave;
static UINT64 gFwEvtSave;
static UINT16 gFwEvtSegSave;
static UINT64 gFwErdpSave;   /* 固件 ERDP：勿清环后强行改回基址 */
/*
 * HCSPARAMS2 MaxScratchpadBufs（与 Linux HCS_MAX_SCRATCHPAD 一致）：
 *   bits 25:21 = Hi（高 5 位）
 *   bits 31:27 = Lo（低 5 位）
 *   count = (Hi << 5) | Lo
 * 旧式把 Hi/Lo 对调会少/多配页 → 装环后写 RS 时 DMA 踩错 → 真机硬挂。
 */
#define XHCI_SCRATCH_MAX 128
static UINT64 gScratchPtr[XHCI_SCRATCH_MAX] __attribute__((aligned(64)));
static UINT8  gScratchBuf[XHCI_SCRATCH_MAX][4096] __attribute__((aligned(4096)));
static UINT8  gDevCtx[2048] __attribute__((aligned(64)));
static UINT8  gHubDevCtx[2048] __attribute__((aligned(64))); /* PR-H-hub */
static UINT8  gInCtx[2048] __attribute__((aligned(64)));
static UINT8  gCtrlBuf[256] __attribute__((aligned(64)));
static UINT8  gReportBuf[8] __attribute__((aligned(64)));
static UINT8  gGetReportBuf[8] __attribute__((aligned(64))); /* 与中断 IN 缓冲分离 */
static UINT8  gErst[16] __attribute__((aligned(64)));

static UINT32 gHubSlotId;
static UINT32 gHubRootPort;
static UINT8  gHubNumPorts;
static UINT8  gHubSpeed;

static volatile UINT32 gCmdDone;
static UINT32 gCmdCode;
static UINT32 gCmdSlot;
static volatile UINT32 gXferDone;
static UINT32 gXferCode;
static UINT32 gXferRemain;
static volatile UINT32 gIntrDone;

/* 读 MMIO 32 位 */
static inline UINT32 ReadMmio32(UINT64 Addr) {
    return *(volatile UINT32 *)(UINTN)Addr;
}

/* 写 MMIO 32 位 */
static inline void WriteMmio32(UINT64 Addr, UINT32 Value) {
    *(volatile UINT32 *)(UINTN)Addr = Value;
}

/* 写 MMIO 64 位（分两次 32 位写） */
static void WriteMmio64(UINT64 Addr, UINT64 Value) {
    WriteMmio32(Addr, (UINT32)Value);
    WriteMmio32(Addr + 4, (UINT32)(Value >> 32));
}

static UINT64 ReadMmio64(UINT64 Addr) {
    UINT64 Lo = ReadMmio32(Addr);
    UINT64 Hi = ReadMmio32(Addr + 4);
    return Lo | (Hi << 32);
}

static void FlushDma(const void *Ptr, UINTN Size);

static void DcbaaSet(UINT32 Slot, UINT64 Phys) {
    if (!gDcbaaLive || Slot > gDcbaaMaxSlot) {
        return;
    }
    gDcbaaLive[Slot] = Phys;
    FlushDma(&gDcbaaLive[Slot], sizeof(UINT64));
}

static void DcbaaFlush(void) {
    if (!gDcbaaLive) {
        return;
    }
    FlushDma(gDcbaaLive, sizeof(UINT64) * (gDcbaaMaxSlot + 1));
}

/* 虚拟地址转物理地址（恒等映射） */
static UINT64 PointerToPhysical(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

/* 内存屏障，保证 TRB 写入对硬件可见 */
static void Fence(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

/* 把 DMA 缓冲从 CPU cache 推出去（真机 RS 后 DMA 读环/DCBAA） */
static void FlushDma(const void *Ptr, UINTN Size) {
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
static void ZeroMemory(void *Ptr, UINTN Size) {
    UINT8 *P = (UINT8 *)Ptr;
    while (Size--) {
        *P++ = 0;
    }
}

static void BootLog(const char *Text);

/* 等待寄存器 Mask 位清零 */
static int WaitClear(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (!(ReadMmio32(Addr) & Mask)) {
            return 1;
        }
    }
    return 0;
}

/* 等待寄存器 Mask 位置位 */
static int WaitSet(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (ReadMmio32(Addr) & Mask) {
            return 1;
        }
    }
    return 0;
}

static UINT64 ReadTsc(void) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
}

/* 真机忙等，按 ~3GHz 估算。QEMU 不要用长 Stall。 */
static void StallMs(UINT32 Ms) {
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

static int WaitSetMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
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

static int WaitClearMs(UINT64 Addr, UINT32 Mask, UINT32 Ms) {
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

static void BootLogHex(const char *Prefix, UINT64 Value, int Digits) {
    char B[20];
    char Msg[56];
    int n = 0;
    int i = 0;

    HalSerialFormatHex(B, Value, Digits);
    while (Prefix[n] && n < 36) {
        Msg[n] = Prefix[n];
        n++;
    }
    while (B[i] && n < 54) {
        Msg[n++] = B[i++];
    }
    Msg[n++] = '\n';
    Msg[n] = 0;
    BootLog(Msg);
}

static void EnumWhy(const char *Why) {
    gEnumWhy = Why;
    BootLog(Why);
}

/* 期望 vs 实际：xhci OK|FAIL <step> want=<期望> got=<实际> */
static void DiagAppend(char *Msg, int *N, int Cap, const char *S) {
    while (S && *S && *N < Cap - 1) {
        Msg[(*N)++] = *S++;
    }
}

static void DiagChk(const char *Step, int Ok, const char *Want, UINT64 Got, int Digits) {
    char Msg[88];
    char Hex[20];
    int n = 0;

    if (gDiagQuiet) {
        return;
    }
    DiagAppend(Msg, &n, (int)sizeof(Msg), Ok ? "xhci OK " : "xhci FAIL ");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Step);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " want=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Want);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " got=");
    HalSerialFormatHex(Hex, Got, Digits);
    DiagAppend(Msg, &n, (int)sizeof(Msg), Hex);
    if (n < (int)sizeof(Msg) - 1) {
        Msg[n++] = '\n';
    }
    Msg[n] = 0;
    BootLog(Msg);
}

static void DiagChkStr(const char *Step, int Ok, const char *Want, const char *Got) {
    char Msg[88];
    int n = 0;

    DiagAppend(Msg, &n, (int)sizeof(Msg), Ok ? "xhci OK " : "xhci FAIL ");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Step);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " want=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Want);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " got=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Got);
    if (n < (int)sizeof(Msg) - 1) {
        Msg[n++] = '\n';
    }
    Msg[n] = 0;
    BootLog(Msg);
}

static const char *CmdTrbName(UINT32 Control) {
    switch ((Control >> 10) & 0x3F) {
    case TRB_ENABLE_SLOT:
        return "EnableSlot";
    case TRB_DISABLE_SLOT:
        return "DisableSlot";
    case TRB_ADDRESS_DEV:
        return "AddressDev";
    case TRB_CONFIG_EP:
        return "ConfigEP";
    case TRB_EVALUATE_CTX:
        return "EvalCtx";
    case TRB_RESET_EP:
        return "ResetEP";
    case TRB_STOP_EP:
        return "StopEP";
    case TRB_SET_TR_DEQ:
        return "SetTrDeq";
    default:
        return "Command";
    }
}

/* 初始化 TRB 环状态 */
static void InitRing(XHCI_TRB *Ring, RING_STATE *St, UINT32 Size) {
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
static void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control) {
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

static UINT32 TrbType(UINT32 Control) {
    return (Control >> 10) & 0x3F;
}

static int MapXhciDma(UINT64 Phys, UINTN Bytes) {
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
static int ResolveFwCmdRing(UINT64 DeqPhys, UINT32 Rcs,
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
static void ProcessEvents(void) {
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
                        HalSerialWrite(Line);
                        gDiagIntrCcLogged++;
                    }
                }
            }
            if (MouseHit) {
                Matched = 1;
                if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
                    gStatMouseEvt++;
                    gMouseReportReady = 1;
                    gMouseIntrDone = 1;
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
                        HalSerialWrite(Line);
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
                    HalSerialWrite(Line);
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
 * 真机：USBSTS.EINT 已置但 Cycle 对不上时，翻 CCS 再扫一次。
 * 照片：EnableSlot 超时 got=USBSTS 0x18（EINT|PCD）→ 有事件却 ProcessEvents 吃不到。
 */
static void ProcessEventsRealPc(void) {
    UINT32 Sts;

    ProcessEvents();
    Sts = ReadMmio32(gOperationalBase + 4);
    if (gCmdDone || !(Sts & USBSTS_EINT)) {
        return;
    }
    gEvtCcs ^= 1u;
    ProcessEvents();
}

static void QueueIntr(void);
static void QueueMouseIntr(void);
static void KbdPush(void);
static void MousePush(void);
static void ServiceHidCompletions(void);

/* 等待命令环完成事件 */
static int WaitCommand(int Timeout) {
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
                HalSerialBootMark("boot: xhci cmd wait2\n");
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
static void ServiceHidCompletions(void) {
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

static int WaitTransfer(int Timeout) {
    if (!HalCpuIsHypervisor() && Timeout > 200000) {
        Timeout = 200000;
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
static void RingDoorbell(UINT32 Slot, UINT32 Target) {
    Fence();
    WriteMmio32(gDoorbellBase + Slot * 4, Target & 0xFF);
}

/*
 * 命令超时恢复：CA 中止命令环，排空事件，再同步 enqueue。
 * 私有环可 InitRing；固件环只按 CRCR dequeue 重解析，勿盲目清环/切软环。
 */
static void RecoverCommandRing(void) {
    UINT64 Cr;
    UINT64 Ptr;
    UINT32 Rcs;
    int i;
    XHCI_TRB *Base;
    UINT32 Size;
    UINT32 Enq;
    UINT32 Pcs;

    HalSerialBootMark("boot: xhci cmd recover\n");
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
    HalSerialBootMark("boot: xhci cmd ring recovered\n");
}

/* 提交一条命令 TRB 并等待完成；超时则 CA 恢复并重试一次 */
static int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
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

static UINT8 *InSlot(void) {
    return gInCtx + gCtxSize;
}

static UINT8 *InEp(UINT32 Dci) {
    return gInCtx + gCtxSize * (Dci + 1);
}

/* 释放 USB 传统支持（BIOS 移交） */
static void TakeLegacy(void) {
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
    DiagChkStr("TakeLegacy", 0, "USBLEGSUP id=1", "not found");
}

/* 真机：BootMark 直写帧缓冲（不 Present）；QEMU 正常串口/GOP */
static void BootLog(const char *Text) {
    if (!HalCpuIsHypervisor()) {
        HalSerialBootMark(Text);
        return;
    }
    HalSerialWrite(Text);
}

/* 停 RS，避免无 HID 时事件环/遗留状态拖死后续 */
static void HaltControllerQuiet(void) {
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
static int ResetController(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    Cmd &= ~USBCMD_RS;
    WriteMmio32(gOperationalBase, Cmd);
    if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        HalSerialWrite("boot: xhci halt timeout\n");
        DebugWrite("XHCI: halt timeout\n");
        return 0;
    }
    WriteMmio32(gOperationalBase, USBCMD_HCRST);
    if (!WaitClear(gOperationalBase, USBCMD_HCRST, 1000000) || !WaitClear(gOperationalBase + 4, USBSTS_CNR, 1000000)) {
        HalSerialWrite("boot: xhci reset timeout\n");
        DebugWrite("XHCI: reset timeout\n");
        return 0;
    }
    return 1;
}

/* 真机：只停 RS，不做 HCRST（该机 HCRST 后再 set RS 会挂） */
static int HaltOnly(void) {
    UINT32 Cmd = ReadMmio32(gOperationalBase);
    if (Cmd & USBCMD_RS) {
        WriteMmio32(gOperationalBase, Cmd & ~USBCMD_RS);
        if (!WaitSet(gOperationalBase + 4, USBSTS_HCH, 200000)) {
            return 0;
        }
    }
    return 1;
}

/* 真机：分阶 set RS 黄字 */
static void BootMarkRs(char Kind, char Stage) {
    char Msg[32];
    int n = 0;
    const char *P = "boot: xhci ";
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
    HalSerialBootMark(Msg);
}

/* 分配 DCBAA、建环并 Run 控制器 */
static int StartController(UINT32 MaxSlots) {
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
            HalSerialBootMark("boot: xhci fw DCBAAP=0\n");
            return 0;
        }
        MapBytes = (UINTN)(MaxSlots + 1) * sizeof(UINT64);
        if (MapBytes < 0x1000) {
            MapBytes = 0x1000;
        }
        if (MapXhciDma(FwDcbaap, MapBytes) != 0) {
            HalSerialBootMark("boot: xhci map DCBAAP fail\n");
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
        HalSerialBootMark("boot: xhci use fw DCBAAP\n");

        /*
         * 真机：DCBAAP 必须固件（否则 RS 挂）；命令/事件环改私有。
         * 固件事件环多次同步失败：EnableSlot 超时 USBSTS=0x18(EINT|PCD)
         * 却吃不到 CMD_COMPLETION（Cycle/dequeue 失步）。
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
        HalSerialBootMark("boot: xhci priv rings\n");

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
            DiagChk("StartController.CRR", 0, "CRR=1", ReadMmio32(gOperationalBase + 0x18), 8);
            HalSerialBootMark("boot: xhci CRR TO\n");
        } else {
            DiagChk("StartController.fwRS", 1, "HCH=0+CRR", ReadMmio32(gOperationalBase + 4), 8);
        }
        HalSerialBootMark("boot: xhci RS running\n");
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
        HalSerialWrite(B);
        HalSerialWrite("\n");
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
    HalSerialWrite(B);
    HalSerialWrite("\n");
    HalSerialBootMark("boot: xhci before RS\n");
    WriteMmio32(gOperationalBase, USBCMD_RS | USBCMD_INTE);
    Fence();
    HalSerialBootMark("boot: xhci after RS\n");

    if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 1000000)) {
        Sts = ReadMmio32(gOperationalBase + 4);
        DiagChk("StartController.RS", 0, "HCH=0", Sts, 8);
        BootLog("boot: xhci run timeout\n");
        return 0;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    DiagChk("StartController.RS", !(Sts & USBSTS_HCH), "HCH=0 running", Sts, 8);
    HalSerialBootMark("boot: xhci RS running\n");
    (void)gDcbaaFromFirmware;
    return 1;
}

static UINT32 PortReg(UINT32 Port1) {
    return 0x400 + (Port1 - 1) * 0x10;
}

static UINT8 PortSpeed(UINT32 Portsc) {
    return (UINT8)((Portsc >> PORTSC_SPEED_SHIFT) & 0xF);
}

static UINT32 PortscNeutral(UINT32 State) {
    return (State & PORTSC_RO) | (State & PORTSC_RWS);
}

/*
 * 清 PORTSC 变更位（W1C）— 仅 QEMU/virt 路径使用。
 * 真机照片两轮：Neutral 清法与 SeaBIOS(PED|PP|CHANGE) 清法都会在
 * PED 已置位后把口打回 0x6E1/0xAE1（Polling）；故真机 ResetPort 不清变更。
 */
static void PortscClearChange(UINT64 Ps) {
    UINT32 Val = ReadMmio32(Ps);
    WriteMmio32(Ps, PORTSC_PED | PORTSC_PP | (Val & PORTSC_CHANGE));
    Fence();
}

/* 已连接口上电；CCS=0 时再给所有口上 PP（PPC 控制器否则看不见设备） */
static void PowerConnectedPorts(void) {
    UINT32 p;
    UINT32 Surveyed = 0;
    int RealPc = !HalCpuIsHypervisor();

    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (Val & PORTSC_CCS) {
            Surveyed++;
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
            }
        }
    }
    if (Surveyed == 0) {
        BootLog("boot: xhci CCS=0, PP all\n");
        for (p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT64 Ps = gOperationalBase + PortReg(p);
            UINT32 Val = ReadMmio32(Ps);
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
            }
        }
    }
    if (RealPc) {
        StallMs(50);
    } else {
        volatile int D;
        for (D = 0; D < 50000; D++) {
        }
    }
}
/*
 * 标准化端口复位（xHCI）：
 * USB2：PP → CCS → PR（勿写 PED=0）→ 等 PRC → 等 PED+CCS。
 * USB3：WPR。
 * 真机：已 PED 勿再 PR；成功后不清变更（sticky PRC）；仅 PED=0 时可清 sticky。
 */
static int ResetPort(UINT32 Port1) {
    UINT64 Ps = gOperationalBase + PortReg(Port1);
    UINT32 Val = ReadMmio32(Ps);
    UINT32 SpeedHint = PortSpeed(Val);
    UINT32 Speed;
    int RealPc = !HalCpuIsHypervisor();
    int i;
    int t;
    int Ok;

    DiagChk("ResetPort.enter", 1, "PORTSC", Val, 8);

    /* 真机：已使能则跳过 PR（二次 PR 会把 0x00200E03 打成 0x002006E1） */
    if (RealPc && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        DiagChk("ResetPort.already", 1, "PED+CCS skip PR", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        return 1;
    }

    /* 端口上电（勿在已连接时清 PED） */
    if (!(Val & PORTSC_PP)) {
        WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        Ok = RealPc ? WaitSetMs(Ps, PORTSC_PP, 50) : WaitSet(Ps, PORTSC_PP, 30000);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PP", Ok && (Val & PORTSC_PP), "PP=1", Val, 8);
        if (!Ok) {
            EnumWhy("boot: why=PP timeout\n");
            return 0;
        }
    }

    if (!(Val & PORTSC_CCS)) {
        if (RealPc) {
            for (i = 0; i < 50; i++) {
                StallMs(10);
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        } else {
            for (i = 0; i < 50000; i++) {
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        }
        DiagChk("ResetPort.CCS", !!(Val & PORTSC_CCS), "CCS=1", Val, 8);
        if (!(Val & PORTSC_CCS)) {
            return 0;
        }
    }

    /* PED=0 时清 sticky 变更安全；便于重新 PR */
    if (RealPc && !(Val & PORTSC_PED) && (Val & PORTSC_CHANGE)) {
        WriteMmio32(Ps, PortscNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
        Fence();
        StallMs(5);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.clrSticky", !(Val & PORTSC_PRC), "PRC=0", Val, 8);
    }

    SpeedHint = PortSpeed(Val);
    DiagChk("ResetPort.speed", SpeedHint != 0, "spd!=0", SpeedHint, 2);

    /*
     * 无条件热复位。USB2：写 PR，保留 PP；不要 &~PED（禁用口）。
     * 硬件会在复位过程中自行清 PED，完成后置 PED。
     */
    Val = PortscNeutral(ReadMmio32(Ps));
    if (SpeedHint >= 4) {
        WriteMmio32(Ps, Val | PORTSC_WPR | PORTSC_PP);
    } else {
        WriteMmio32(Ps, Val | PORTSC_PR | PORTSC_PP);
    }
    Fence();

    if (RealPc) {
        /*
         * 照片：PED OK → 任意 afterClr → Polling。成功后 leavePRC。
         * PRC 可先到而 PED 仍 0（0x002006E1），多等一会 PED。
         */
        Ok = WaitSetMs(Ps, PORTSC_PRC | PORTSC_WRC, 500);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
        if (!Ok) {
            EnumWhy("boot: why=reset timeout\n");
            return 0;
        }
        if (!(Val & PORTSC_PED)) {
            Ok = WaitSetMs(Ps, PORTSC_PED, 1000);
            Val = ReadMmio32(Ps);
        }
        DiagChk("ResetPort.PED", (Val & PORTSC_PED) && (Val & PORTSC_CCS),
                "PED+CCS", Val, 8);
        if (!(Val & PORTSC_PED) || !(Val & PORTSC_CCS)) {
            if (!(Val & PORTSC_CCS)) {
                EnumWhy("boot: why=lost CCS\n");
            } else {
                EnumWhy("boot: why=not PED\n");
            }
            return 0;
        }
        DiagChk("ResetPort.leavePRC", 1, "sticky PRC", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        StallMs(50);
        return 1;
    }

    Ok = WaitSet(Ps, PORTSC_PRC | PORTSC_WRC, 60000);
    Val = ReadMmio32(Ps);
    DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
    if (!Ok) {
        return 0;
    }
    if (!(Val & PORTSC_PED)) {
        for (t = 0; t < 30000; t++) {
            Val = ReadMmio32(Ps);
            if (Val & PORTSC_PED) {
                break;
            }
        }
    }
    PortscClearChange(Ps);
    for (t = 0; t < 30000; t++) {
        Val = ReadMmio32(Ps);
        if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
            DiagChk("ResetPort.done", 1, "PED+CCS", Val, 8);
            return 1;
        }
    }
    DiagChk("ResetPort.PED", 0, "PED+CCS", ReadMmio32(Ps), 8);
    return 0;
}

static UINT16 SpeedMps(UINT8 Speed) {
    if (Speed == 4) {
        return 512;
    }
    if (Speed == 3) {
        return 64;
    }
    return 8;
}

static int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx, UINT32 RouteString,
                               UINT8 ParentHubSlot, UINT8 TtPort,
                               int HubDevice, UINT8 HubNumPorts) {
    int Ok;

    gXferSlot = 0;
    if (SlotOut) {
        *SlotOut = 0;
    }
    DiagChk("AddressDev.port", 1, "root+spd", ((UINT64)RootPort << 8) | Speed, 4);
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), SlotOut) < 0 || *SlotOut == 0 ||
        *SlotOut > gDcbaaMaxSlot) {
        DiagChkStr("AddressDev", 0, "EnableSlot ok", "fail");
        BootLogHex("boot: xhci EnableSlot cc=", gCmdCode, 2);
        BootLogHex("boot: xhci EnableSlot slot=", *SlotOut, 2);
        BootLogHex("boot: xhci EnableSlot done=", gCmdDone, 1);
        EnumWhy("boot: why=enable slot\n");
        return 0;
    }

    gXferSlot = *SlotOut;
    DcbaaSet(*SlotOut, PointerToPhysical(DevCtx));
    ZeroMemory(DevCtx, 2048);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    if (SlotOut == &gSlotId) {
        gKbdRoute = RouteString & 0xFFFFFu;
        gKbdHubSlot = ParentHubSlot;
        gKbdTtPort = TtPort;
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20) | (RouteString & 0xFFFFFu);
    if (HubDevice) {
        Slot[0] |= (1u << 26);
    }
    Slot[1] = ((UINT32)RootPort << 16);
    if (HubDevice && HubNumPorts != 0) {
        Slot[1] |= ((UINT32)HubNumPorts << 24);
    }
    if (ParentHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)ParentHubSlot | ((UINT32)TtPort << 8);
    }

    InitRing(gEp0Ring, &gEp0, RING_SIZE);
    UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
    gEp0Mps = SpeedMps(Speed);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEp0Mps << 16);
    UINT64 Deq = PointerToPhysical(gEp0Ring) | 1;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(DevCtx, 2048);
    DcbaaFlush();
    FlushDma(gEp0Ring, sizeof(gEp0Ring));

    Ok = Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(*SlotOut), 0) == 0;
    DiagChk("AddressDev", Ok, "AddressDev cc=1", gCmdCode, 2);
    if (!Ok) {
        BootLogHex("boot: xhci addr cc=", gCmdCode, 2);
        EnumWhy("boot: why=address fail\n");
        return 0;
    }
    return 1;
}

/* Enable Slot + Address Device（根口设备） */
static int AddressDevice(UINT32 Port1, UINT8 Speed) {
    gXferSlot = gSlotId;
    return AddressDeviceOnPort(Port1, Speed, &gSlotId, gDevCtx, 0, 0, 0, 0, 0);
}

static void DisableSlot(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DCBAA_SLOTS) {
        return;
    }
    (void)Command(0, TRB_TYPE(TRB_DISABLE_SLOT) | TRB_SLOT(SlotId), 0);
    DcbaaSet(SlotId, 0);
    if (gSlotId == SlotId) {
        gSlotId = 0;
    }
    if (gMouseSlotId == SlotId) {
        gMouseSlotId = 0;
    }
    if (gHubSlotId == SlotId) {
        gHubSlotId = 0;
    }
    if (gXferSlot == SlotId) {
        gXferSlot = 0;
    }
}

/* EP0 控制传输（SETUP-DATA-STATUS） */
static int ControlXfer(USB_SETUP_PACKET *Setup, void *Data) {
    UINT64 SetupParam = 0;
    UINT8 *Raw = (UINT8 *)Setup;
    for (int i = 0; i < 8; i++) {
        SetupParam |= ((UINT64)Raw[i]) << (8 * i);
    }

    UINT32 Trt = 0;
    if (Setup->wLength && Data) {
        Trt = (Setup->bmRequestType & 0x80) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    gXferDone = 0;
    Enqueue(gEp0Ring, &gEp0, SetupParam, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | Trt);

    if (Setup->wLength && Data) {
        UINT32 Dir = (Setup->bmRequestType & 0x80) ? TRB_DIR_IN : 0;
        Enqueue(gEp0Ring, &gEp0, PointerToPhysical(Data), Setup->wLength, TRB_TYPE(TRB_DATA) | Dir);
    }

    UINT32 StatusDir = (Setup->wLength && (Setup->bmRequestType & 0x80)) ? 0 : TRB_DIR_IN;
    Enqueue(gEp0Ring, &gEp0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDoorbell(gXferSlot, 1);
    if (WaitTransfer(150000) < 0) {
        DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
        return -1;
    }
    DiagChk("ControlXfer", gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET,
            "cc=1|13", gXferCode, 2);
    return 0;
}

/* GET_DESCRIPTOR 控制传输封装 */
static int GetDesc(UINT16 TypeIndex, UINT16 Index, UINT16 Length, void *Buf) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x80,
        .bRequest = 0x06,
        .wValue = TypeIndex,
        .wIndex = Index,
        .wLength = Length
    };
    ZeroMemory(Buf, Length);
    FlushDma(Buf, Length);
    if (ControlXfer(&Setup, Buf) < 0) {
        return -1;
    }
    FlushDma(Buf, Length);
    return 0;
}

static int EvaluateEp0(UINT32 SlotId, UINT16 Mps) {
    UINT64 Deq;

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 1);
    {
        UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
        Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)Mps << 16);
        Deq = PointerToPhysical(&gEp0Ring[gEp0.Enq]) | (UINT64)(gEp0.Pcs & 1);
        Ep0[2] = (UINT32)Deq;
        Ep0[3] = (UINT32)(Deq >> 32);
    }
    gEp0Mps = Mps;
    FlushDma(gInCtx, sizeof(gInCtx));
    return Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_EVALUATE_CTX) | TRB_SLOT(SlotId), 0) == 0;
}

/* 先 8 字节拿 bMaxPacketSize0，再 18 字节完整设备描述符 */
static int GetDeviceDesc(void) {
    UINT8 Mps;
    int Ok;

    Ok = GetDesc(0x0100, 0, 8, gCtrlBuf) == 0;
    DiagChk("GetDesc8", Ok, "xfer ok", Ok ? gCtrlBuf[7] : gXferCode, 2);
    if (!Ok) {
        EnumWhy("boot: why=desc8\n");
        return -1;
    }
    Mps = gCtrlBuf[7];
    if (Mps != 8 && Mps != 16 && Mps != 32 && Mps != 64) {
        Mps = (UINT8)gEp0Mps;
    }
    if (Mps != (UINT8)gEp0Mps) {
        (void)EvaluateEp0(gXferSlot, Mps);
    }
    Ok = GetDesc(0x0100, 0, 18, gCtrlBuf) == 0;
    DiagChk("GetDesc18", Ok, "len>=18 class", Ok ? gCtrlBuf[4] : gXferCode, 2);
    if (!Ok) {
        EnumWhy("boot: why=desc18\n");
        return -1;
    }
    return 0;
}

/* SET_CONFIGURATION 请求 */
static int SetConfig(UINT8 Config) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x00,
        .bRequest = 0x09,
        .wValue = Config,
        .wIndex = 0,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_PROTOCOL Boot 协议 */
static int SetProtocolBoot(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0B,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_IDLE 请求 */
static int SetIdle(UINT8 Iface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0A,
        .wValue = 0x0000,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/* HID SET_REPORT：输出报告（键盘 LED 等） */
static int SetReportOutput(UINT8 Iface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x09,
        .wValue = 0x0200,
        .wIndex = Iface,
        .wLength = Length
    };
    return ControlXfer(&Setup, Data);
}

/* HID GET_REPORT(Input)：中断 IN 不来时真机 poll 兜底（独立缓冲，勿占 gReportBuf） */
static int HidGetInputReport(UINT32 Slot, UINT8 Iface, void *Buf, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0xA1,
        .bRequest = 0x01,
        .wValue = 0x0100,
        .wIndex = Iface,
        .wLength = Length
    };
    UINT32 QuietSave;

    if (Slot == 0 || Length == 0 || !Buf) {
        return -1;
    }
    gXferSlot = Slot;
    ZeroMemory(Buf, Length);
    FlushDma(Buf, Length);
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1; /* 读秒时勿刷 ControlXfer OK 盖白字 */
    if (ControlXfer(&Setup, Buf) < 0) {
        gDiagQuiet = QuietSave;
        return -1;
    }
    gDiagQuiet = QuietSave;
    FlushDma(Buf, Length);
    return 0;
}

static UINT8 FsInterval(UINT8 BInterval) {
    if (BInterval == 0) {
        BInterval = 1;
    }
    UINT8 Log2 = 0;
    UINT8 V = BInterval;
    while (V > 1) {
        V >>= 1;
        Log2++;
    }
    return (UINT8)(Log2 + 3);
}

/* 配置 HID 中断 IN 端点 */
static int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    gIntrDci = (UINT32)EpNum * 2 + In;
    gKbdEpAddr = EpAddr;

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gIntrDci);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = ((UINT32)gIntrDci << 27) | ((UINT32)Speed << 20) | (gKbdRoute & 0xFFFFFu);
    Slot[1] = (UINT32)gPort1 << 16;
    if (gKbdHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gKbdHubSlot | ((UINT32)gKbdTtPort << 8);
    }

    InitRing(gIntrRing, &gIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gIntrRing, sizeof(gIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        DebugWrite("XHCI: Configure Endpoint failed\n");
        EnumWhy("boot: why=cfg ep\n");
        return 0;
    }
    DebugWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/*
 * 真机 Arm：枚举期已挂中断 TRB。须先 Stop（环仍有效）→ 排空 Stopped 事件
 * → 再 InitRing → Set TR Dequeue；失败则 Reset EP 再试。
 * 旧序 InitRing 先于 Stop 会毁掉 HC 还在用的环，且 Stop 回调里 QueueIntr
 * 会导致 SetTrDeq 报 Context State Error (got=0x13)。
 */
static int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St,
                           UINTN RingBytes) {
    UINT64 Deq;
    UINT32 EpField = (Dci & 0x1Fu) << 16;

    if (Slot == 0 || Dci == 0) {
        return -1;
    }

    /* 1) 先停 EP（此时环内容仍与硬件一致） */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(Slot) | EpField, 0);
    ProcessEvents();
    if (!HalCpuIsHypervisor()) {
        ProcessEventsRealPc();
    }

    /* 2) 软件环从头重建，再告诉 HC 新 dequeue */
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EpField, 0) == 0) {
        return 0;
    }

    /* 3) Context State 等：Reset EP 后再 SetTrDeq */
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(Slot) | EpField, 0);
    ProcessEvents();
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RingBytes);
    Deq = PointerToPhysical(&Ring[St->Enq]) | (St->Pcs & 1u);
    if (Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(Slot) | EpField, 0) < 0) {
        HalSerialWrite("boot: xhci sync deq fail\n");
        return -1;
    }
    return 0;
}

/* 提交中断 IN 传输 TRB */
static void QueueIntr(void) {
    gIntrDone = 0;
    gIntrReportReady = 0;
    FlushDma(gReportBuf, sizeof(gReportBuf));
    Enqueue(gIntrRing, &gIntr, PointerToPhysical(gReportBuf), 8, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    RingDoorbell(gSlotId, gIntrDci);
}

static int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                       UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 CurScore = 0;
    UINT8 BestScore = 0;
    UINT8 CurIface = 0;
    *Iface = 0;
    *EpAddr = 0;
    *Mps = 8;
    *Interval = 10;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            UINT8 Sub = Cfg[Off + 6];
            UINT8 Proto = Cfg[Off + 7];
            CurScore = 0;
            /* 3/1/1 boot keyboard 最优；3/1/0 次之；3/0/x 亦试（真机常见） */
            if (Class == 3 && Sub == 1 && Proto == 1) {
                CurScore = 3;
            } else if (Class == 3 && Sub == 1 && Proto == 0) {
                CurScore = 2;
            } else if (Class == 3 && Sub != 1) {
                CurScore = 1;
            }
            CurIface = Cfg[Off + 2];
        } else if (Type == 5 && Len >= 7 && CurScore) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                *Iface = CurIface;
                *EpAddr = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                *Interval = Cfg[Off + 6];
                (void)Speed;
                if (BestScore == 3) {
                    return 1;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    return BestScore != 0;
}

static int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundIface = 0;
    UINT8 BestProto = 0xFF;
    UINT8 BestIface = 0;
    UINT8 BestEp = 0;
    UINT16 BestMps = 8;
    UINT8 BestInterval = 10;
    UINT8 CurProto = 0;

    *Iface = 0;
    *EpAddr = 0;
    *Mps = 8;
    *Interval = 10;

    /*
     * 只要 HID Class=3 且不是 boot keyboard（Protocol=1）。
     * 优先 boot mouse Protocol=2，避免把键盘接口配成「鼠标」后
     * 键码被 MousePush 当成绝对坐标（NUC：按键光标乱跳、真鼠标不动）。
     */
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            UINT8 Proto = Cfg[Off + 7];
            CurProto = Proto;
            if (Class == 3 && Proto != 1) {
                FoundIface = 1;
                *Iface = Cfg[Off + 2];
            } else {
                FoundIface = 0;
            }
        } else if (Type == 5 && Len >= 7 && FoundIface) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03)) {
                UINT16 ThisMps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                UINT8 ThisIv = Cfg[Off + 6];
                /* Protocol 2（boot mouse）最优；否则接受第一个非键盘 HID */
                if (CurProto == 2 || BestProto == 0xFF ||
                    (BestProto != 2 && CurProto < BestProto)) {
                    BestProto = CurProto;
                    BestIface = *Iface;
                    BestEp = Addr;
                    BestMps = ThisMps;
                    BestInterval = ThisIv;
                }
                if (CurProto == 2) {
                    break;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    (void)Speed;
    if (BestProto == 0xFF) {
        return 0;
    }
    *Iface = BestIface;
    *EpAddr = BestEp;
    *Mps = BestMps;
    *Interval = BestInterval;
    return 1;
}

static int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                              UINT8 Speed) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    gMouseIntrDci = (UINT32)EpNum * 2 + In;
    gMouseEpAddr = EpAddr;
    gMouseReportLen = (UINT8)(Mps > 8 ? 8 : Mps);

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = ((UINT32)gMouseIntrDci << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)gMousePort << 16;

    InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0)
                                  : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    return 1;
}

static void QueueMouseIntr(void) {
    gMouseIntrDone = 0;
    gMouseReportReady = 0;
    FlushDma(gMouseBuf, sizeof(gMouseBuf));
    Enqueue(gMouseIntrRing, &gMouseIntr, PointerToPhysical(gMouseBuf), gMouseReportLen,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    RingDoorbell(gMouseSlotId, gMouseIntrDci);
}

static int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                          int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                         UINT16 *, UINT8 *),
                          int UseBootProto) {
    gXferSlot = SlotId;
    (void)DevCtx;

    for (volatile int d = 0; d < 500000; d++) {
    }

    InitRing(gEp0Ring, &gEp0, RING_SIZE);

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
    int HaveIntr = ParseFn(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval);
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    if (UseBootProto) {
        SetProtocolBoot(Iface);
    }
    SetIdle(Iface);
    return HaveIntr;
}

/* ---- PR-H-hub：一层 USB2 hub（根口 Class 9）---- */

#define HUB_PORT_CONNECTION   (1u << 0)
#define HUB_PORT_ENABLE       (1u << 1)
#define HUB_PORT_RESET        (1u << 4)
#define HUB_PORT_POWER        (1u << 8)
#define HUB_C_PORT_CONNECTION (1u << 16)
#define HUB_C_PORT_RESET      (1u << 20)
#define HUB_FEAT_PORT_RESET   4
#define HUB_FEAT_PORT_POWER   8
#define HUB_FEAT_C_PORT_RESET 20

static int HubCtrl(UINT8 BmReq, UINT8 Req, UINT16 Value, UINT16 Index,
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

static int TryConfigureKeyboardSlot(UINT8 Speed) {
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
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    (void)SetProtocolBoot(gKbdIface);
    SetIdle(gKbdIface);
    if (!ConfigureIntr(EpAddr, Mps, Interval, Speed)) {
        return 0;
    }
    ZeroMemory(gReportBuf, 8);
    QueueIntr();
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

static int EnumHubChildrenForKeyboard(void) {
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
        if (!(St & HUB_PORT_ENABLE) && !(St & HUB_PORT_CONNECTION)) {
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

/* 根口已 Address 且 device desc 在 gCtrlBuf：若是 hub 则枚举子口找键盘 */
static int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed) {
    UINT8 HubDesc[16];
    UINT8 Nports = 4;
    UINT8 ConfigVal = 1;

    if (!IsHubDeviceDesc()) {
        return 0;
    }
    BootLog("boot: xhci hub on root\n");
    /* 重新 Address 为 Hub 设备（带 Hub 位） */
    DisableSlot(gSlotId);
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    gEp0Mps = SpeedMps(Speed);
    if (!AddressDeviceOnPort(RootPort, Speed, &gHubSlotId, gHubDevCtx,
                             0, 0, 0, 1, 8)) {
        gHubSlotId = 0;
        EnumWhy("boot: why=hub addr\n");
        return 0;
    }
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        DisableSlot(gHubSlotId);
        return 0;
    }
    ConfigVal = gCtrlBuf[5] ? gCtrlBuf[5] : 1;
    if (SetConfig(ConfigVal) < 0) {
        DisableSlot(gHubSlotId);
        return 0;
    }
    ZeroMemory(HubDesc, sizeof(HubDesc));
    if (HubCtrl(0xA0, 0x06, 0x2900, 0, sizeof(HubDesc), HubDesc) == 0 &&
        HubDesc[2] != 0) {
        Nports = HubDesc[2];
        if (Nports > 15) {
            Nports = 15;
        }
    }
    gHubNumPorts = Nports;
    BootLog("boot: xhci hub ports ok\n");
    if (EnumHubChildrenForKeyboard()) {
        return 1;
    }
    return 0;
}

static int InitMouseOnPort(UINT32 Port1) {
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port1));
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    if (!ResetPort(Port1)) {
        return 0;
    }
    UINT8 Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
    gMousePort = Port1;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
        gMouseSlotId = 0;
        return 0;
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
        DebugWrite("XHCI: mouse config failed\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }

    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 Iface = 0;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
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
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        DebugWrite("XHCI: mouse no interrupt EP\n");
        BootLog("boot: xhci skip non-mouse HID\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    gMouseIface = Iface;
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    DebugWrite("XHCI: mouse ready\n");
    BootLog("boot: xhci-hid mouse\n");
    return 1;
}

/* 完整 xHCI 初始化：复位、建环、枚举端口上的 USB 键盘 */
int XhciInit(UINT64 BaseAddress) {
    int RealPc = !HalCpuIsHypervisor();
    char B[12];

    BootLog("xhci diag: OK|FAIL step want=期望 got=实际\n");

    /*
     * 实测：白字最后停在 ports=0x12 且无 B10 黄字 → Present 在该行可能不返回。
     * 真机：一进 Init 就 mute，ports/探针全走 BootMark（直写帧缓冲）。
     */
    if (RealPc) {
        HalSerialGopMute(1);
        HalSerialBootMark("boot: xhci-Hhid enter\n");
        gXhciDmar = -2;
        gXhciTe = -2;
        {
            UINT64 Rsdp = HalPlatformRsdp();
            int Dmar;
            int Te;
            if (Rsdp == 0) {
                HalSerialBootMark("boot: xhci RSDP=0\n");
            } else {
                HalSerialBootMark("boot: xhci RSDP ok\n");
                Dmar = AcpiTablePresent(Rsdp, "DMAR");
                gXhciDmar = Dmar;
                if (Dmar > 0) {
                    HalSerialBootMark("boot: xhci DMAR=yes\n");
                    HalSerialBootMark("boot: xhci TE off...\n");
                    Te = AcpiDmarDisableTranslation(Rsdp);
                    gXhciTe = Te;
                    if (Te == 2) {
                        HalSerialBootMark("boot: xhci TE was ON->off\n");
                    } else if (Te == 1) {
                        HalSerialBootMark("boot: xhci TE already off\n");
                    } else if (Te == 0) {
                        HalSerialBootMark("boot: xhci TE no DRHD\n");
                    } else {
                        HalSerialBootMark("boot: xhci TE off fail\n");
                    }
                } else if (Dmar == 0) {
                    HalSerialBootMark("boot: xhci DMAR=no\n");
                    gXhciTe = -2;
                } else {
                    HalSerialBootMark("boot: xhci DMAR=bad\n");
                }
            }
        }
    }

    if (BaseAddress == 0) {
        if (RealPc) {
            HalSerialBootMark("boot: xhci null BAR\n");
            HalSerialGopMute(0);
        } else {
            HalSerialWrite("boot: xhci null BAR\n");
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
            HalSerialBootMark("boot: xhci bad CAP\n");
            HalSerialGopMute(0);
        } else {
            HalSerialWrite("boot: xhci bad CAP=");
            HalSerialFormatHex(B, Cap, 8);
            HalSerialWrite(B);
            HalSerialWrite("\n");
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
                HalSerialBootMark("boot: xhci map ERST fail\n");
            } else {
                Erst = (UINT8 *)(UINTN)gFwErstbaSave;
                gFwEvtSave = *(UINT64 *)(void *)Erst;
                gFwEvtSegSave = *(UINT16 *)(void *)(Erst + 8);
            }
        }
        if (gFwCrcrSave == 0 || gFwErstbaSave == 0) {
            HalSerialBootMark("boot: xhci snap ring=0\n");
        } else {
            HalSerialBootMark("boot: xhci snap rings ok\n");
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
        HalSerialBootMark(Msg);
    } else {
        HalSerialWrite("boot: xhci ports=");
        HalSerialWrite(B);
        HalSerialWrite("\n");
    }

    /*
     * PR-H-hub：真机不再 B14 裸 RS 后 return；HaltOnly（避免 HCRST）→ Start → 枚举。
     * 失败则 unmute，让 PS/2 有机会 Probe。
     */
    BootLog("boot: xhci take legacy...\n");
    TakeLegacy();
    BootLog("boot: xhci after legacy\n");

    if (RealPc) {
        HalSerialBootMark("boot: xhci-Hhid halt\n");
        if (!HaltOnly()) {
            HalSerialBootMark("boot: xhci halt fail\n");
            HalSerialGopMute(0);
            HalSerialWrite("boot: xhci halt fail, desktop\n");
            return 0;
        }
        if (!StartController(MaxSlots)) {
            HalSerialBootMark("boot: xhci start fail\n");
            HalSerialGopMute(0);
            HalSerialWrite("boot: xhci start fail, desktop\n");
            HaltControllerQuiet();
            return 0;
        }
    } else {
        if (!ResetController() || !StartController(MaxSlots)) {
            return 0;
        }
    }
    DiagChkStr("XhciInit.run", 1, "controller running", "yes");
    HalSerialWrite("boot: xhci controller running\n");
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
            HalSerialWrite("boot: xhci CCS ports=");
            HalSerialFormatHex(B, Surveyed, 2);
            HalSerialWrite(B);
            HalSerialWrite("\n");
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

    {
        int PassMax = 3;
        for (int Wait = 0; Wait < PassMax && Port1 == 0; Wait++) {
            HalSerialWrite("boot: xhci enum pass=");
            {
                char B[12];
                HalSerialFormatHex(B, (UINT64)(UINT32)(Wait + 1), 2);
                HalSerialWrite(B);
                HalSerialWrite("\n");
            }
            for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
                UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(p));
                if (!(Ps & PORTSC_CCS)) {
                    continue;
                }
                HalSerialWrite("boot: xhci try port=");
                {
                    char B[12];
                    HalSerialFormatHex(B, p, 2);
                    HalSerialWrite(B);
                    HalSerialWrite("\n");
                }
                if (!ResetPort(p)) {
                    continue;
                }
                UINT32 After = ReadMmio32(gOperationalBase + PortReg(p));
                Speed = PortSpeed(After);
                gPort1 = p;
                gSpeed = Speed;

                HalSerialBootMark("boot: xhci address...\n");
                if (!AddressDevice(p, Speed)) {
                    HalSerialBootMark("boot: xhci addr fail\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                HalSerialBootMark("boot: xhci address ok\n");

                HalSerialBootMark("boot: xhci get desc\n");
                if (GetDeviceDesc() < 0) {
                    HalSerialBootMark("boot: xhci desc fail\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                /* PR-H-hub：根口 hub → 子口找键盘 */
                if (IsHubDeviceDesc()) {
                    HalSerialBootMark("boot: xhci hub root\n");
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
                    UINT16 Total = (UINT16)(gCtrlBuf[2] | (gCtrlBuf[3] << 8));
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
                    ConfigVal = gCtrlBuf[5];
                    if (ConfigVal == 0) {
                        ConfigVal = 1;
                    }
                    HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps,
                                           &Interval);
                }
                if (!HaveIntr) {
                    EnumWhy("boot: why=no hid ep\n");
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
                if (!ConfigureIntr(EpAddr, Mps, Interval, Speed)) {
                    DisableSlot(gSlotId);
                    continue;
                }
                ZeroMemory(gReportBuf, 8);
                QueueIntr();
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

    if (RealPc) {
        HalSerialGopMute(0);
    }

    if (Port1 == 0) {
        HalSerialWrite("boot: xhci up but no HID keyboard\n");
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
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    /* 与 mouse 同走 BootLog：真机屏上先 keyboard 再 mouse，再由 Probe 打 init returned */
    BootLog("boot: xhci-hid keyboard\n");

    for (UINT32 p = 1; p <= gMaxPorts; p++) {
        if (p == gPort1) {
            continue;
        }
        if (InitMouseOnPort(p)) {
            break;
        }
    }

    return 1;
}

int XhciHidKeyboardReady(void) {
    return gSlotId != 0;
}

/* 将键盘报告推入环形软件队列 */
static void KbdPush(void) {
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

static void MousePush(void) {
    UINT32 Next = (gMouseWriteIndex + 1) % MOUSE_Q;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    int UseAbsolute;

    if (Next == gMouseReadIndex) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseWriteIndex];
    R->Wheel = 0;
    R->Absolute = 0;
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    /*
     * 绝对坐标启发式曾把 boot 键盘报告（[mod,0,keycode,0…]）当成平板：
     * 键码落在 X 高字节 → 光标乱跳。仅当 16-bit X/Y 的高字节都非 0
     * （真平板常见），才走绝对路径；否则一律相对 boot 鼠标。
     */
    UseAbsolute = 0;
    if (gMouseReportLen >= 6 && X0 <= 32767 && Y0 <= 32767 &&
        (gMouseBuf[2] != 0) && (gMouseBuf[4] != 0)) {
        UseAbsolute = 1;
    } else if (gMouseReportLen >= 7 && X1 <= 32767 && Y1 <= 32767 &&
               (gMouseBuf[3] != 0) && (gMouseBuf[5] != 0)) {
        UseAbsolute = 2;
    }

    if (UseAbsolute == 1) {
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
        R->Absolute = 1;
        if (gMouseReportLen >= 6) {
            R->Wheel = (INT8)gMouseBuf[5];
        }
    } else if (UseAbsolute == 2) {
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
        R->Absolute = 1;
        if (gMouseReportLen >= 7) {
            R->Wheel = (INT8)gMouseBuf[6];
        }
    } else {
        /* HID boot 相对鼠标：b0 buttons, b1 X, b2 Y, b3 wheel */
        static int AbsX = 512;
        static int AbsY = 384;
        static int AbsInit;
        int Dx = (int)(signed char)gMouseBuf[1];
        int Dy = (int)(signed char)gMouseBuf[2];
        if (!AbsInit) {
            AbsInit = 1;
        }
        AbsX += Dx;
        AbsY += Dy;
        if (AbsX < 0) {
            AbsX = 0;
        }
        if (AbsY < 0) {
            AbsY = 0;
        }
        if (AbsX > 3840) {
            AbsX = 3840;
        }
        if (AbsY > 2160) {
            AbsY = 2160;
        }
        R->X = (UINT32)AbsX;
        R->Y = (UINT32)AbsY;
        R->Buttons = gMouseBuf[0] & 7;
        if (gMouseReportLen >= 4) {
            R->Wheel = (INT8)gMouseBuf[3];
        }
    }
    gMouseWriteIndex = Next;
}

/* 清除中断管理器挂起位 */
static void ImClearPending(void) {
    UINT32 Im = ReadMmio32(gRuntimeBase + 0x20);
    WriteMmio32(gRuntimeBase + 0x20, Im | 1u);
}

/* XHCI MSI-X 中断处理：处理事件、重新排队中断传输 */
void XhciIrq(void) {
    SpinLockAcquire(&gHidQueueLock);
    ProcessEvents();
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
static void EnableHostInterrupts(void) {
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
 * 排空事件环。POLL 与 DUAL 都必须盲 ProcessEvents（backup）。
 * 仅将来 XHCI_IRQ_MODE_IRQ 才可考虑减弱 Drain（PR-H-xhci-irq）。
 *
 * 真机证据：把键盘误配成鼠标时，poll 排空能把键码送进 GUI。
 * 开 MSI 后若关掉盲 Drain，NUC 上完成 TRB 会堆死。
 */
void XhciDrainEvents(void) {
    int i;
    int RealPc = !HalCpuIsHypervisor();

    gStatDrain++;
    SpinLockAcquire(&gHidQueueLock);
    for (i = 0; i < 32; i++) {
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
     * 真机：中断 IN 优先。仅当尚无中断完成（i=0）时才 GET_REPORT，
     * 且用独立缓冲，避免与 QueueIntr 的 gReportBuf 打架。
     */
    if (RealPc) {
        static UINT8 gGetReportNote;
        static UINT8 WasDown;
        static UINT8 MouseWas;
        if (gSlotId != 0 && gIntrDci != 0) {
            if ((gStatDrain & 0xFFu) == 0) {
                RingDoorbell(gSlotId, gIntrDci);
            }
            if (gStatIntrEvt == 0 && (gStatDrain & 0x7FFu) == 0) {
                if (HidGetInputReport(gSlotId, gKbdIface, gGetReportBuf, 8) == 0) {
                    int nz = 0;
                    int b;
                    for (b = 0; b < 8; b++) {
                        gReportBuf[b] = gGetReportBuf[b];
                        if (gGetReportBuf[b]) {
                            nz = 1;
                        }
                    }
                    if (nz || WasDown) {
                        KbdPush();
                        gStatKbdPush++;
                    }
                    WasDown = (UINT8)nz;
                    if (!gGetReportNote) {
                        HalSerialWrite("boot: xhci get-report poll\n");
                        gGetReportNote = 1;
                    }
                }
            }
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            if ((gStatDrain & 0xFFu) == 0) {
                RingDoorbell(gMouseSlotId, gMouseIntrDci);
            }
            if (gStatMouseEvt == 0 && (gStatDrain & 0x7FFu) == 0) {
                UINT16 Len = gMouseReportLen ? gMouseReportLen : 8;
                UINT8 Tmp[8];
                if (Len > 8) {
                    Len = 8;
                }
                if (HidGetInputReport(gMouseSlotId, gMouseIface, Tmp, Len) == 0) {
                    int nz = 0;
                    int b;
                    for (b = 0; b < (int)Len; b++) {
                        gMouseBuf[b] = Tmp[b];
                        if (Tmp[b]) {
                            nz = 1;
                        }
                    }
                    if (nz || MouseWas) {
                        MousePush();
                        gStatMousePush++;
                    }
                    MouseWas = (UINT8)nz;
                    if (!gGetReportNote) {
                        HalSerialWrite("boot: xhci get-report poll\n");
                        gGetReportNote = 1;
                    }
                }
            }
        }
    }
    if (gUseIrq && gRuntimeBase != 0) {
        ImClearPending();
    }
    SpinLockRelease(&gHidQueueLock);
}

/* dual/irq → 切回 poll 备份（关 host IE；不拆 PCI MSI 表亦可，避免半残状态） */
void XhciFallbackToPoll(const char *Why) {
    UINT32 Cmd;

    gUseIrq = 0;
    gIrqMode = XHCI_IRQ_MODE_POLL;
    if (gOperationalBase != 0) {
        Cmd = ReadMmio32(gOperationalBase);
        WriteMmio32(gOperationalBase, (Cmd | USBCMD_RS) & ~USBCMD_INTE);
    }
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 0); /* clear IE */
    }
    HalSerialWrite("boot: xhci irq=poll (fallback)");
    if (Why && Why[0]) {
        HalSerialWrite(" ");
        HalSerialWrite(Why);
    }
    HalSerialWrite("\n");
    XhciDrainEvents();
}

/*
 * PR-H-xhci-dual 占位：真机 base 未通前不武装 MSI-X。
 * 实现时应：PciEnableMsi → EnableHostInterrupts → gIrqMode=DUAL、gUseIrq=1，
 * 且 Drain 仍盲排空；探针失败则 XhciFallbackToPoll。
 */
int XhciTryEnterDual(USB_CONTROLLER *Device) {
    (void)Device;
    if (gIrqMode == XHCI_IRQ_MODE_DUAL) {
        return 1;
    }
    /* 占位：保持 POLL，供路线图/日后开刀接线 */
    HalSerialWrite("boot: xhci dual=stub (hold poll base)\n");
    gIrqMode = XHCI_IRQ_MODE_POLL;
    gUseIrq = 0;
    return 0;
}

XHCI_IRQ_MODE XhciIrqMode(void) {
    return gIrqMode;
}

/* PHOTO：t=任意xfer i=键鼠匹配 k/m=推送 u=未匹配 se=最近slot.ep c=cc r=环事件 d=drain */
void XhciDiagFormat(char *Buf, int Max) {
    char Dig[12];
    int N = 0;
    UINT32 V[8];
    int vi;
    const char *Tags = "tikmucrd"; /* 紧凑标签；se 单独拼 */

    if (!Buf || Max < 8) {
        return;
    }
    V[0] = gStatXferAny;
    V[1] = gStatIntrEvt + gStatMouseEvt;
    V[2] = gStatKbdPush;
    V[3] = gStatMousePush;
    V[4] = gStatUnmatched;
    V[5] = gStatLastCc;
    V[6] = gStatEvtRing;
    V[7] = gStatDrain;
    Buf[0] = 0;
    for (vi = 0; vi < 8 && N + 14 < Max; vi++) {
        int t = 0;
        UINT32 X = V[vi];
        /* c 与 se 之间插入 se=；c 在 Tags[5] */
        if (vi == 5 && N + 16 < Max) {
            Buf[N++] = ' ';
            Buf[N++] = 's';
            Buf[N++] = '=';
            {
                UINT32 S = gStatLastSlot;
                if (S >= 100) {
                    S = 99;
                }
                Buf[N++] = (char)('0' + (S / 10));
                Buf[N++] = (char)('0' + (S % 10));
            }
            Buf[N++] = '.';
            {
                UINT32 E = gStatLastEp;
                if (E >= 100) {
                    E = 99;
                }
                Buf[N++] = (char)('0' + (E / 10));
                Buf[N++] = (char)('0' + (E % 10));
            }
        }
        Buf[N++] = ' ';
        Buf[N++] = Tags[vi];
        Buf[N++] = '=';
        if (X == 0) {
            Buf[N++] = '0';
            Buf[N] = 0;
            continue;
        }
        while (X && t < 10) {
            Dig[t++] = (char)('0' + (X % 10));
            X /= 10;
        }
        while (t > 0 && N + 1 < Max) {
            Buf[N++] = Dig[--t];
        }
        Buf[N] = 0;
    }
}

/* Arm 后打一枪：期望的键鼠 slot/DCI，便于对照 s=. */
void XhciDiagLogArms(void) {
    char Line[96];
    int n = 0;
    const char *P = "boot: xhci arms kbd=";
    while (*P && n < 28) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + ((gSlotId / 10) % 10));
    Line[n++] = (char)('0' + (gSlotId % 10));
    Line[n++] = '/';
    Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
    Line[n++] = (char)('0' + (gIntrDci % 10));
    P = " mouse=";
    while (*P && n < 48) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + ((gMouseSlotId / 10) % 10));
    Line[n++] = (char)('0' + (gMouseSlotId % 10));
    Line[n++] = '/';
    Line[n++] = (char)('0' + ((gMouseIntrDci / 10) % 10));
    Line[n++] = (char)('0' + (gMouseIntrDci % 10));
    Line[n++] = '\n';
    Line[n] = 0;
    HalSerialWrite(Line);
}

/*
 * QEMU：可开 MSI。真机：只进 POLL（base）；dual 走 XhciTryEnterDual。
 */
int XhciEnableIrq(USB_CONTROLLER *Device) {
    UINT8 Dest;

    if (gUseGetReport || (gSlotId == 0 && gMouseSlotId == 0)) {
        DebugWrite("XHCI: no interrupt EP, IRQ unused\n");
        gUseIrq = 0;
        gIrqMode = XHCI_IRQ_MODE_POLL;
        HalSerialWrite("boot: xhci irq=none\n");
        return 0;
    }
    /* 真机：H-xhci-base — 零 MSI，留下 dual 占位入口 */
    if (!HalCpuIsHypervisor()) {
        gUseIrq = 0;
        gIrqMode = XHCI_IRQ_MODE_POLL;
        HalSerialWrite("boot: xhci irq=poll (base)\n");
        (void)XhciTryEnterDual(Device); /* stub：打 dual=stub 行，不改模式 */
        /* PHOTO 只看 Arm 之后的计数 */
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
        gDiagXferLogged = 0;
        gDiagIntrCcLogged = 0;
        XhciDiagLogArms();
        /* 真机：先同步 EP dequeue，再投递；失败则仍尝试 Queue（枚举环可能仍可用） */
        if (gSlotId != 0 && gIntrDci != 0) {
            if (SyncIntrDequeue(gSlotId, gIntrDci, gIntrRing, &gIntr, sizeof(gIntrRing)) == 0) {
                HalSerialWrite("boot: xhci sync kbd deq\n");
            }
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            if (SyncIntrDequeue(gMouseSlotId, gMouseIntrDci, gMouseIntrRing, &gMouseIntr,
                                sizeof(gMouseIntrRing)) == 0) {
                HalSerialWrite("boot: xhci sync mouse deq\n");
            }
            QueueMouseIntr();
        }
        /* Arm 同步产生的 Stopped 事件勿计入 PHOTO */
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
        HalSerialWrite("boot: xhci rearm intr\n");
        XhciDrainEvents();
        return 0;
    }
    if (PciEnableMsi(Device, VEC_XHCI)) {
        EnableHostInterrupts();
        gUseIrq = 0;
        XhciDrainEvents();
        gUseIrq = 1;
        gIrqMode = XHCI_IRQ_MODE_DUAL; /* QEMU：中断+Drain 同形，视为 dual 课堂形 */
        HalSerialWrite("boot: xhci irq=msi\n");
        return 1;
    }
    Dest = HalCpuApicId(0);
    if (PciEnableIoApicIntx(Device, VEC_XHCI, Dest)) {
        EnableHostInterrupts();
        gUseIrq = 0;
        XhciDrainEvents();
        gUseIrq = 1;
        gIrqMode = XHCI_IRQ_MODE_DUAL;
        HalSerialWrite("boot: xhci irq=ioapic\n");
        return 1;
    }
    DebugWrite("XHCI: MSI/IOAPIC failed; poll drain\n");
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
