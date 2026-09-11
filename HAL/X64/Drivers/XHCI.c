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
#include "ToySerialLog.h"
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
#define EVT_SIZE            128 /* 真机 PHOTO→gui 空窗期需更大；原 32 易溢满导致桌面假死 */
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
#define TRB_ISP             (1u << 2) /* 短包也要完成事件（鼠标常见） */
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
static int gXhciStarted; /* 已对某 BAR 完成 Start；无 HID 时可 Abandon 再试下一颗 */
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
static UINT8  gKbdParseScore; /* ParseConfig：3=boot键 2=3/1/0 1=其它HID */
static UINT8  gKbdEpAddr; /* 配置描述符 bEndpointAddress，匹配事件用 */
static UINT16 gKbdMps;    /* ConfigureIntr 记下的 MPS；composite 重建用 */
static UINT8  gKbdEpInterval; /* 已换算进 EP 上下文的 Interval 字段 */
static UINT8  gUseGetReport;
static UINT8  gKbdPollReport; /* 真机复合键鼠：键中断 IN 常 k=0，改 EP0 GET_REPORT */
static UINT8  gKbdReportPrev[8];
static volatile UINT32 gGetReportBusy;
static UINT8  gGetReportFails;
static UINT8  gXferFast; /* GET_REPORT 用短超时，避免拖死鼠标 */
static UINT8  gUseIrq;
/* 真机默认 POLL；DUAL 见 XhciTryEnterDual（EnableIrq / Arm） */
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
static UINT32 gMouseRoute;   /* hub 子设备 Route String；根口设备为 0 */
static UINT8  gMouseHubSlot; /* TT：父 hub slot；根口为 0 */
static UINT8  gMouseTtPort;
static UINT32 gMouseIntrDci;
static UINT8  gMouseIface;
static UINT8  gMouseIfaceProto; /* bInterfaceProtocol：2=boot 相对；0=tablet 等绝对 */
static UINT8  gMouseParseScore; /* ParseConfigMouse 评分：3=boot鼠 2=boot子类 1=其它HID */
static UINT8  gMouseAbsolute;   /* 1：报告为绝对坐标（QEMU usb-tablet） */
static UINT8  gMouseEpAddr;
static UINT8  gMouseReportLen;
static UINT8  gMouseXferLen; /* 最近一次中断 IN 实际字节（短包后 < MPS） */
static UINT8  gMouseBuf[8] __attribute__((aligned(64)));
/* boot 相对鼠：在驱动内累加成屏坐标；PHOTO→桌面时重置到光标 */
static int    gMouseAbsX = 512;
static int    gMouseAbsY = 384;
static int    gMouseAbsInit;
static XHCI_TRB gMouseIntrRing[RING_SIZE] __attribute__((aligned(64)));
static RING_STATE gMouseIntr;
/* PR-H-msc-2：Bulk 静态环（仅 InitRing；不配 EP、不门铃、不扫口） */
static XHCI_TRB gBulkInRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gBulkOutRing[RING_SIZE] __attribute__((aligned(64)));
static RING_STATE gBulkIn;
static RING_STATE gBulkOut;
static int gMscBulkRingsInited;
/* PR-H-msc-3：scan 临时 slot，独立 EP0，勿 InitRing 键盘 gEp0 */
static UINT32 gMscScanSlot;
static UINT8  gMscScanDevCtx[2048] __attribute__((aligned(64)));
static XHCI_TRB gMscScanEp0Ring[RING_SIZE] __attribute__((aligned(64)));
static RING_STATE gMscScanEp0;
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
static volatile UINT32 gStatIrq;       /* PR-H-xhci-stat：XhciIrq 进入次数 */
static UINT32 gDiagXferLogged;        /* 限制串口/屏日志条数 */
static UINT32 gDiagQuiet;             /* GET_REPORT poll：勿 DiagChk 刷屏/盖白字 */
static UINT32 gDiagIntrCcLogged;
static UINT32 gCtrlFailLogged;        /* ControlXfer FAIL 最多抄几条到 PHOTO */

/*
 * 串口日志级别（默认安静）：
 *   make XHCI_DIAG_VERBOSE=1  → 全量 OK DiagChk + 逐步 BootMark
 *   默认 0                    → 只打 FAIL + 键鼠/hub 里程碑（好抄 PHOTO）
 */
#ifndef XHCI_DIAG_VERBOSE
#define XHCI_DIAG_VERBOSE 0
#endif

static int DiagVerbose(void) {
    return XHCI_DIAG_VERBOSE != 0;
}

#define MOUSE_Q 32
static USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
static volatile UINT32 gMouseWriteIndex;
static volatile UINT32 gMouseReadIndex;
static SPIN_LOCK gHidQueueLock; /* PR-S-ap：IRQ 入队 vs AP 出队 */

static XHCI_TRB gCmdRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEp0Ring[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gHubEp0Ring[RING_SIZE] __attribute__((aligned(64)));   /* hub 专用：勿与键鼠共环 */
static XHCI_TRB gMouseEp0Ring[RING_SIZE] __attribute__((aligned(64))); /* 独立鼠/子设备专用 */
static XHCI_TRB gIntrRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEvtRing[EVT_SIZE] __attribute__((aligned(64)));
/* 真机可指向固件环（IOMMU 已映射）；QEMU 用上面静态缓冲 */
static XHCI_TRB *gCmdRingLive = gCmdRing;
static XHCI_TRB *gEvtRingLive = gEvtRing;
static UINT32 gEvtRingSize = EVT_SIZE;

static RING_STATE gCmd;
static RING_STATE gEp0;
static RING_STATE gHubEp0;
static RING_STATE gMouseEp0;
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
static UINT8  gErst[16] __attribute__((aligned(64)));

static UINT32 gHubSlotId;
static UINT32 gHubRootPort;
static UINT8  gHubNumPorts;
static UINT8  gHubSpeed;
static UINT8  gHubMtt; /* bDeviceProtocol==2 才置 MTT；误置单 TT hub 会导致子设备中断永不完成 */
static UINT8  gHubTtt; /* Hub Desc wHubCharacteristics[6:5] → Slot TT Think Time */
static UINT32 gPortNoHid; /* 键盘 pass 已判非 HID 的根口（如前面 U 盘） */
static UINT32 gPortNeedForcePr; /* 本轮已 Address+Disable，再扫须强制 PR */
/* Address 走 gEp0 的 slot：claim 成鼠标后仍须用 gEp0，勿切 gMouseEp0 */
static UINT8  gSlotEp0UsesKbdRing[DCBAA_SLOTS + 1];

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

static void CopyMemory(void *Dst, const void *Src, UINTN Size) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    while (Size--) {
        *D++ = *S++;
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

static void BootLogV(const char *Text) {
    if (DiagVerbose()) {
        BootLog(Text);
    }
}

static void BootLogHexV(const char *Prefix, UINT64 Value, int Digits) {
    if (DiagVerbose()) {
        BootLogHex(Prefix, Value, Digits);
    }
}

static void BootMarkV(const char *Text) {
    if (DiagVerbose()) {
        ToyBootMarkUsb(Text);
    }
}

static void EnumWhy(const char *Why) {
    gEnumWhy = Why;
    BootLog(Why);
}

/* 期望 vs 实际：默认只打 FAIL；VERBOSE=1 时 OK 也打 */
static void DiagAppend(char *Msg, int *N, int Cap, const char *S) {
    while (S && *S && *N < Cap - 1) {
        Msg[(*N)++] = *S++;
    }
}

static void DiagChk(const char *Step, int Ok, const char *Want, UINT64 Got, int Digits) {
    char Msg[88];
    char Hex[20];
    int n = 0;

    /* 安静：关掉 OK；FAIL 的 want=/got= 仍上 BootLog（PHOTO 能抄），除非 gDiagQuiet */
    if (gDiagQuiet) {
        return;
    }
    if (Ok && !DiagVerbose()) {
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

    if (gDiagQuiet) {
        return;
    }
    if (Ok && !DiagVerbose()) {
        return;
    }
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
static void ProcessEventsRealPc(void) {
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

static void QueueIntr(void);
static void QueueMouseIntr(void);
static void KbdPush(void);
static void MousePush(void);
static void ServiceHidCompletions(void);
static int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
static int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                              UINT8 Speed);
static int SetInterface(UINT8 Iface, UINT8 Alt);
static int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St,
                           UINTN RingBytes);
static UINT8 FsInterval(UINT8 BInterval);

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
    /* 真机常见无 USBLEGSUP：安静模式不刷 FAIL */
    if (DiagVerbose()) {
        DiagChkStr("TakeLegacy", 0, "USBLEGSUP id=1", "not found");
    }
}

/* 真机：BootMark 直写帧缓冲（不 Present）；QEMU 正常串口/GOP */
static void BootLog(const char *Text) {
    if (!HalCpuIsHypervisor()) {
        ToyBootMarkUsb(Text);
        return;
    }
    ToyLogUsb(Text);
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

/* 真机：分阶 set RS 黄字（仅 VERBOSE） */
static void BootMarkRs(char Kind, char Stage) {
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

/*
 * 已连接口上电。
 * 刀1/刀2（笔记本 CCS=0，不动 NUC/工控已成功路径）：
 *   - 已有 CCS：行为与旧相同（真机仅 Stall 50ms）。
 *   - 全 0：PP all → 打 maxports → 多轮等待复扫 → 仍 0 则打前几口 PORTSC。
 */
static void PowerConnectedPorts(void) {
    UINT32 p;
    UINT32 Surveyed = 0;
    int RealPc = !HalCpuIsHypervisor();
    int Round;
    UINT32 DumpLimit;

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
    if (Surveyed > 0) {
        /* NUC/工控：口上已有设备，保持原短等待 */
        if (RealPc) {
            StallMs(50);
        } else {
            volatile int D;
            for (D = 0; D < 50000; D++) {
            }
        }
        return;
    }

    BootLog("boot: xhci CCS=0, PP all\n");
    BootLogHex("boot: xhci maxports=", gMaxPorts, 2);
    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (!(Val & PORTSC_PP)) {
            WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        }
    }

    if (!RealPc) {
        volatile int D;
        for (D = 0; D < 50000; D++) {
        }
        return;
    }

    /* 仅无 CCS：加长等待（USB3 口挂 USB2 鼠常见） */
    for (Round = 0; Round < 8; Round++) {
        StallMs(100);
        Surveyed = 0;
        for (p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT64 Ps = gOperationalBase + PortReg(p);
            UINT32 Val = ReadMmio32(Ps);
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
                Val = ReadMmio32(Ps);
            }
            if (Val & PORTSC_CCS) {
                Surveyed++;
            }
        }
        if (Surveyed > 0) {
            BootLogHex("boot: xhci CCS after wait=", Surveyed, 2);
            return;
        }
    }

    /* 普查已扫完全部口仍无 CCS；打齐最多 8 口 PORTSC（Intel 常 USB2/3 分口编号） */
    DumpLimit = gMaxPorts;
    if (DumpLimit > 8) {
        DumpLimit = 8;
    }
    for (p = 1; p <= DumpLimit; p++) {
        UINT32 Val = ReadMmio32(gOperationalBase + PortReg(p));
        char Pref[32];
        int n = 0;
        const char *S = "boot: xhci PORTSC";
        while (*S && n < 24) {
            Pref[n++] = *S++;
        }
        if (p >= 10) {
            Pref[n++] = (char)('0' + (p / 10));
            Pref[n++] = (char)('0' + (p % 10));
        } else {
            Pref[n++] = (char)('0' + p);
        }
        Pref[n++] = '=';
        Pref[n] = 0;
        BootLogHex(Pref, Val, 8);
    }
}
/*
 * 标准化端口复位（xHCI）：
 * USB2：PP → CCS → PR（勿写 PED=0）→ 等 PRC → 等 PED+CCS。
 * USB3：WPR。
 * 真机：已 PED 勿再 PR；成功后不清变更（sticky PRC）；仅 PED=0 时可清 sticky。
 */
/*
 * Force=0：真机已 PED+CCS 则跳过 PR（同 pass 内二次 PR 易打坏口）。
 * Force=1：鼠标等二次枚举须 PR（DisableSlot 后设备仍 PED，不重置会 cc=0x04）。
 */
static int ResetPortEx(UINT32 Port1, int Force) {
    UINT64 Ps = gOperationalBase + PortReg(Port1);
    UINT32 Val = ReadMmio32(Ps);
    UINT32 SpeedHint = PortSpeed(Val);
    UINT32 Speed;
    int RealPc = !HalCpuIsHypervisor();
    int i;
    int t;
    int Ok;

    DiagChk("ResetPort.enter", 1, "PORTSC", Val, 8);

    if (RealPc && !Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        DiagChk("ResetPort.already", 1, "PED+CCS skip PR", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        return 1;
    }
    if (RealPc && Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        BootLogHexV("boot: xhci port PR force=", Port1, 2);
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

static int ResetPort(UINT32 Port1) {
    return ResetPortEx(Port1, 0);
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

/*
 * 键 / hub / 独立鼠 各用独立 EP0 环。
 * 真机证据：Address 子设备时 InitRing(共享 gEp0Ring) 会毁掉 hub EP0 dequeue，
 * hub Slot 仍 Hub=1 且鼠 epst=Running，但 TT 中断 IN 永不完成 → PHOTO m=0。
 */
static void Ep0RingForSlot(UINT32 SlotId, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotId != 0 && SlotId == gHubSlotId) {
        *RingOut = gHubEp0Ring;
        *StOut = &gHubEp0;
    } else if (SlotId != 0 && SlotId == gMscScanSlot) {
        *RingOut = gMscScanEp0Ring;
        *StOut = &gMscScanEp0;
    } else if (SlotId != 0 && SlotId <= DCBAA_SLOTS && gSlotEp0UsesKbdRing[SlotId]) {
        /* 曾以键盘路径 Address：claim 为鼠后仍跟 gEp0 硬件 dequeue */
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    } else if (SlotId != 0 && SlotId == gMouseSlotId && SlotId != gSlotId) {
        *RingOut = gMouseEp0Ring;
        *StOut = &gMouseEp0;
    } else {
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    }
}

static void Ep0RingForSlotOut(UINT32 *SlotOut, XHCI_TRB **RingOut, RING_STATE **StOut) {
    if (SlotOut == &gHubSlotId) {
        *RingOut = gHubEp0Ring;
        *StOut = &gHubEp0;
    } else if (SlotOut == &gMouseSlotId) {
        *RingOut = gMouseEp0Ring;
        *StOut = &gMouseEp0;
    } else if (SlotOut == &gMscScanSlot) {
        *RingOut = gMscScanEp0Ring;
        *StOut = &gMscScanEp0;
    } else {
        *RingOut = gEp0Ring;
        *StOut = &gEp0;
    }
}

static int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx, UINT32 RouteString,
                               UINT8 ParentHubSlot, UINT8 TtPort,
                               int HubDevice, UINT8 HubNumPorts) {
    int Ok;
    XHCI_TRB *Ep0Ring;
    RING_STATE *Ep0St;

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
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[*SlotOut] = 1;
        }
    }
    if (SlotOut == &gMouseSlotId) {
        gMouseRoute = RouteString & 0xFFFFFu;
        gMouseHubSlot = ParentHubSlot;
        gMouseTtPort = TtPort;
        if (*SlotOut <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[*SlotOut] = 0;
        }
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20) | (RouteString & 0xFFFFFu);
    if (HubDevice) {
        Slot[0] |= (1u << 26); /* USB2 hub only; SS hub must stay Hub=0 */
        if (gHubMtt) {
            Slot[0] |= (1u << 25); /* 仅 Multi-TT hub */
        }
    }
    Slot[1] = ((UINT32)RootPort << 16);
    if (HubDevice && HubNumPorts != 0) {
        Slot[1] |= ((UINT32)HubNumPorts << 24);
    }
    if (ParentHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)ParentHubSlot | ((UINT32)TtPort << 8);
    }

    Ep0RingForSlotOut(SlotOut, &Ep0Ring, &Ep0St);
    InitRing(Ep0Ring, Ep0St, RING_SIZE);
    UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
    gEp0Mps = SpeedMps(Speed);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEp0Mps << 16);
    UINT64 Deq = PointerToPhysical(Ep0Ring) | 1;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(DevCtx, 2048);
    DcbaaFlush();
    FlushDma(Ep0Ring, RING_SIZE * sizeof(XHCI_TRB));

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
    gSlotEp0UsesKbdRing[SlotId] = 0;
    if (gSlotId == SlotId) {
        gSlotId = 0;
    }
    if (gMouseSlotId == SlotId) {
        gMouseSlotId = 0;
    }
    if (gHubSlotId == SlotId) {
        gHubSlotId = 0;
    }
    if (gMscScanSlot == SlotId) {
        gMscScanSlot = 0;
    }
    if (gXferSlot == SlotId) {
        gXferSlot = 0;
    }
}

/* ControlXfer 超时后 EP0 环与 HC 失步，须 Reset+SetTrDeq 才能继续枚举 */
static void RecoverEp0(UINT32 SlotId) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;
    UINT32 EpField = (1u << 16);
    UINT32 QuietSave;

    if (SlotId == 0) {
        return;
    }
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1; /* 恢复过程中的 Stop/ResetEP 勿刷 FAIL */
    (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    (void)Command(0, TRB_TYPE(TRB_RESET_EP) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    Ep0RingForSlot(SlotId, &Ring, &St);
    InitRing(Ring, St, RING_SIZE);
    FlushDma(Ring, RING_SIZE * sizeof(XHCI_TRB));
    Deq = PointerToPhysical(Ring) | 1;
    (void)Command(Deq, TRB_TYPE(TRB_SET_TR_DEQ) | TRB_SLOT(SlotId) | EpField, 0);
    ProcessEvents();
    gDiagQuiet = QuietSave;
}

/* EP0 控制传输（SETUP-DATA-STATUS） */
static int ControlXfer(USB_SETUP_PACKET *Setup, void *Data) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 SetupParam = 0;
    UINT8 *Raw = (UINT8 *)Setup;
    for (int i = 0; i < 8; i++) {
        SetupParam |= ((UINT64)Raw[i]) << (8 * i);
    }

    UINT32 Trt = 0;
    if (Setup->wLength && Data) {
        Trt = (Setup->bmRequestType & 0x80) ? TRB_TRT_IN : TRB_TRT_OUT;
    }

    Ep0RingForSlot(gXferSlot, &Ring, &St);

    /* 清完成码：超时后若仍显示上一笔 cc=1，会误报 FAIL want=cc=1|13 got=0x01 */
    gXferDone = 0;
    gXferCode = 0;
    Enqueue(Ring, St, SetupParam, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | Trt);

    if (Setup->wLength && Data) {
        UINT32 Dir = (Setup->bmRequestType & 0x80) ? TRB_DIR_IN : 0;
        Enqueue(Ring, St, PointerToPhysical(Data), Setup->wLength, TRB_TYPE(TRB_DATA) | Dir);
    }

    UINT32 StatusDir = (Setup->wLength && (Setup->bmRequestType & 0x80)) ? 0 : TRB_DIR_IN;
    Enqueue(Ring, St, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDoorbell(gXferSlot, 1);
    if (WaitTransfer(150000) < 0) {
        ProcessEvents();
        ServiceHidCompletions();
        if (gXferDone && (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET)) {
            DiagChk("ControlXfer", 1, "cc=1|13", gXferCode, 2);
            return 0;
        }
        if (!gXferDone) {
            if (DiagVerbose()) {
                DiagChkStr("ControlXfer", 0, "xfer done", "timeout");
            }
        } else if (!gXferFast && gCtrlFailLogged < 2) {
            /* Stall(6) 在 GET_REPORT 轮询时很常见；限 2 条免刷屏 */
            DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
            gCtrlFailLogged++;
        }
        RecoverEp0(gXferSlot);
        return -1;
    }
    if (!(gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET)) {
        if (!gXferFast && gCtrlFailLogged < 2) {
            DiagChk("ControlXfer", 0, "cc=1|13", gXferCode, 2);
            gCtrlFailLogged++;
        }
        return -1;
    }
    if (DiagVerbose()) {
        DiagChk("ControlXfer", 1, "cc=1|13", gXferCode, 2);
    }
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

/* USB2 hub：对齐 EDK2 XhcConfigHubContext —— 从 Output Slot 拷贝后 ConfigEP，写入 Hub/TTT/MTT/端口数。
 * 仅 Evaluate 且不带 TTT 时，真机常见 EP0 经 TT 成功、中断 IN 永不完成（PHOTO m=0）。 */
static int EvaluateHubSlot(UINT32 SlotId, UINT32 RootPort, UINT8 Speed, UINT8 NumPorts) {
    UINT32 *InSlotCtx;
    UINT32 *OutSlotCtx;
    UINT32 i;
    UINT32 Words;

    if (Speed >= 4 || NumPorts == 0 || SlotId == 0) {
        return 0;
    }
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0); /* Add A0 */
    InSlotCtx = (UINT32 *)(void *)InSlot();
    FlushDma(gHubDevCtx, 2048);
    OutSlotCtx = (UINT32 *)(void *)gHubDevCtx;
    Words = gCtxSize / 4u;
    if (Words > 16) {
        Words = 16;
    }
    for (i = 0; i < Words; i++) {
        InSlotCtx[i] = OutSlotCtx[i];
    }
    /* Context Entries 至少 1；Hub + 可选 MTT + TTT */
    if (((InSlotCtx[0] >> 27) & 0x1Fu) < 1u) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0x1Fu << 27)) | (1u << 27);
    }
    InSlotCtx[0] |= (1u << 26);
    if (gHubMtt) {
        InSlotCtx[0] |= (1u << 25);
    } else {
        InSlotCtx[0] &= ~(1u << 25);
    }
    InSlotCtx[0] = (InSlotCtx[0] & ~(3u << 16)) | (((UINT32)gHubTtt & 3u) << 16);
    if (Speed != 0) {
        InSlotCtx[0] = (InSlotCtx[0] & ~(0xFu << 20)) | ((UINT32)Speed << 20);
    }
    InSlotCtx[1] = (InSlotCtx[1] & 0x0000FFFFu) |
                   ((UINT32)RootPort << 16) | ((UINT32)NumPorts << 24);
    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gHubDevCtx, 2048);
    /* EDK2 走 Configure Endpoint（非 Evaluate）更新 hub Slot */
    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) != 0) {
        BootLogHex("boot: xhci hub cfg cc=", gCmdCode, 2);
        return 0;
    }
    BootLogHex("boot: xhci hub mtt=", gHubMtt, 1);
    BootLogHex("boot: xhci hub ttt=", gHubTtt, 1);
    return 1;
}

/* Device Desc 仍在 gCtrlBuf：HS Multi-TT hub 的 bDeviceProtocol==2 */
static void HubNoteMttFromDevDesc(UINT8 Speed) {
    gHubMtt = 0;
    if (Speed == 3 && gCtrlBuf[4] == 0x09 && gCtrlBuf[7] == 2) {
        gHubMtt = 1;
    }
}

static int EvaluateEp0(UINT32 SlotId, UINT16 Mps) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT64 Deq;

    Ep0RingForSlot(SlotId, &Ring, &St);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 1);
    {
        UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
        Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)Mps << 16);
        Deq = PointerToPhysical(&Ring[St->Enq]) | (UINT64)(St->Pcs & 1);
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
    if (DiagVerbose()) {
        DiagChk("GetDesc8", Ok, "xfer ok", Ok ? gCtrlBuf[7] : gXferCode, 2);
    }
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
    if (DiagVerbose()) {
        DiagChk("GetDesc18", Ok, "len>=18 class", Ok ? gCtrlBuf[4] : gXferCode, 2);
    }
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

/* HID GET_REPORT(Input)：复合设备键盘中断 IN 不完成时的 EP0 兜底 */
static int HidGetInputReport(UINT8 Iface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0xA1,
        .bRequest = 0x01,
        .wValue = 0x0100,
        .wIndex = Iface,
        .wLength = Length
    };
    if (gSlotId == 0) {
        return -1;
    }
    gXferSlot = gSlotId;
    return ControlXfer(&Setup, Data);
}

/*
 * 真机复合：PHOTO 上键 EP(DCI=5) 从不完成、鼠 DCI=3 正常。
 * 在 Drain 释锁后轮询 GET_REPORT；与中断鼠并行，勿持 gHidQueueLock。
 */
static void XhciPollKbdGetReport(void) {
    UINT8 Buf[8];
    int i;
    int Diff;

    if (!gKbdPollReport || gSlotId == 0) {
        return;
    }
    if (gGetReportBusy) {
        return;
    }
    gGetReportBusy = 1;
    ZeroMemory(Buf, sizeof(Buf));
    gXferFast = 1;
    if (HidGetInputReport(gKbdIface, Buf, 8) < 0) {
        gXferFast = 0;
        gGetReportFails++;
        if (gGetReportFails == 1) {
            BootLog("boot: xhci get-report stall/retry\n");
        }
        if (gGetReportFails >= 32) {
            gKbdPollReport = 0;
            BootLog("boot: xhci kbd get-report give up\n");
        }
        gGetReportBusy = 0;
        return;
    }
    gXferFast = 0;
    gGetReportFails = 0;
    Diff = 0;
    for (i = 0; i < 8; i++) {
        if (Buf[i] != gKbdReportPrev[i]) {
            Diff = 1;
            break;
        }
    }
    if (Diff) {
        for (i = 0; i < 8; i++) {
            gKbdReportPrev[i] = Buf[i];
            gReportBuf[i] = Buf[i];
        }
        FlushDma(gReportBuf, sizeof(gReportBuf));
        SpinLockAcquire(&gHidQueueLock);
        KbdPush();
        gStatKbdPush++;
        gStatIntrEvt++;
        SpinLockRelease(&gHidQueueLock);
    }
    gGetReportBusy = 0;
}

/*
 * 曾有 ReAddKbdIntrOnly / RecoverKbdIntr / EnableKbdGetReport：
 * GET_REPORT 易 Stall；recover 未再挂入枚举路径 → 已删，消 unused 警告。
 * HID GET_REPORT 曾作 poll 兜底；持 gHidQueueLock 时调用会死锁，故已从 Drain 移除。
 */

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
/*
 * 首次 ConfigEP。MouseEpAddr!=0 时同一次 Add 键盘+鼠标（真机二次 ConfigEP 会弄死键盘，
 * 含 Add-only；NUC PHOTO：add-only ok 仍 k=0 m 正常）。
 */
static int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed,
                         UINT8 MouseEpAddr, UINT16 MouseMps, UINT8 MouseBInterval) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    UINT8 Interval;
    UINT32 CtxEntries;
    UINT32 AddFlags;

    gIntrDci = (UINT32)EpNum * 2 + In;
    gKbdEpAddr = EpAddr;
    if (Mps == 0 || Mps > 64) {
        Mps = 8;
    }
    gKbdMps = Mps;

    CtxEntries = gIntrDci;
    AddFlags = (1u << 0) | (1u << gIntrDci);
    if (MouseEpAddr != 0) {
        UINT8 MEpNum = MouseEpAddr & 0x0F;
        UINT8 MIn = (MouseEpAddr & 0x80) ? 1 : 0;

        gMouseIntrDci = (UINT32)MEpNum * 2 + MIn;
        gMouseEpAddr = MouseEpAddr;
        if (MouseMps == 0 || MouseMps > 64) {
            MouseMps = 8;
        }
        gMouseReportLen = (UINT8)(MouseMps > 8 ? 8 : MouseMps);
        if (gMouseReportLen < 3) {
            gMouseReportLen = 3;
        }
        if (gMouseIntrDci > CtxEntries) {
            CtxEntries = gMouseIntrDci;
        }
        AddFlags |= (1u << gMouseIntrDci);
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = AddFlags;

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gKbdRoute & 0xFFFFFu);
    Slot[1] = (UINT32)gPort1 << 16;
    if (gKbdHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gKbdHubSlot | ((UINT32)gKbdTtPort << 8);
    }

    InitRing(gIntrRing, &gIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gIntrDci);
    Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    gKbdEpInterval = Interval;
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    /* Average TRB Length | Max ESIT Payload Lo（HID：=MPS；为 0 时部分 HC 不调度中断 IN） */
    Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);

    if (MouseEpAddr != 0) {
        UINT8 MInterval;
        UINT32 *MEp;
        UINT64 MDeq;

        InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
        MEp = (UINT32 *)(void *)InEp(gMouseIntrDci);
        MInterval = (Speed >= 3)
                        ? (UINT8)((MouseBInterval > 0) ? (MouseBInterval - 1) : 0)
                        : FsInterval(MouseBInterval);
        MEp[0] = (UINT32)MInterval << 16;
        MEp[1] = (3u << 1) | (7u << 3) | ((UINT32)MouseMps << 16);
        MDeq = PointerToPhysical(gMouseIntrRing) | 1;
        MEp[2] = (UINT32)MDeq;
        MEp[3] = (UINT32)(MDeq >> 32);
        MEp[4] = (UINT32)MouseMps | ((UINT32)MouseMps << 16);
        FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
    }

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gIntrRing, sizeof(gIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        DebugWrite("XHCI: Configure Endpoint failed\n");
        EnumWhy("boot: why=cfg ep\n");
        if (MouseEpAddr != 0) {
            gMouseIntrDci = 0;
            gMouseEpAddr = 0;
        }
        return 0;
    }
    if (MouseEpAddr != 0) {
        gMouseSlotId = gSlotId;
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        BootLog("boot: xhci mouse with-kbd cfg ok\n");
    }
    {
        char Line[64];
        char Hex[12];
        int n = 0;
        const char *P = "boot: xhci kbd ep=";
        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EpAddr, 2);
        P = Hex;
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    DebugWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/*
 * 在首次 ConfigEP 前：从当前配置描述符认领复合鼠标 iface（SetInterface/Protocol/Idle）。
 * 成功则写出 EP 参数供 ConfigureIntr 一次 Add。
 */
static int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KbdIface, UINT8 KbdEp,
                              UINT8 *EpOut, UINT16 *MpsOut, UINT8 *IvOut) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 CurAlt = 0, BestAlt = 0;
    UINT16 Off;

    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        return 0;
    }
    if (Iface == KbdIface) {
        return 0;
    }
    if ((EpAddr & 0x0F) == (KbdEp & 0x0F) && ((EpAddr ^ KbdEp) & 0x80) == 0) {
        return 0;
    }

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

    *EpOut = EpAddr;
    *MpsOut = Mps;
    *IvOut = Interval;
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
        BootLog(Line);
    }
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
        ToyLogUsb("boot: xhci sync deq fail\n");
        return -1;
    }
    return 0;
}

/* 提交中断 IN：长度用首次 ConfigureIntr 的 MPS（勿超过 8） */
static void QueueIntr(void) {
    UINT32 Len = gKbdMps;

    gIntrDone = 0;
    gIntrReportReady = 0;
    FlushDma(gReportBuf, sizeof(gReportBuf));
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gIntrRing, &gIntr, PointerToPhysical(gReportBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
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
    gKbdParseScore = BestScore;
    return BestScore != 0;
}

/*
 * 真机：罗技 G102 等游戏鼠常带额外 HID（媒体/宏，3/0/x 或 3/1/0），
 * 旧逻辑会当成「键盘」占 slot，再把同设备 boot 鼠绑成 composite →
 * 真键盘口永远轮不到，且假键盘 k=0、鼠却丝滑。
 * 若本设备「键盘分」<3 且已有像样鼠标接口 → 不当键盘，留给 InitMouseOnPort。
 */
static int RealPcRejectMouseExtraAsKeyboard(UINT16 Total, UINT8 Speed) {
    UINT8 MIface = 0, MEp = 0, MIv = 10;
    UINT16 MMps = 8;

    if (HalCpuIsHypervisor()) {
        return 0;
    }
    if (gKbdParseScore >= 3) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &MIface, &MEp, &MMps, &MIv)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }
    BootLog("boot: xhci skip mouse+extraHID as kbd\n");
    {
        char Line[48];
        int n = 0;
        const char *P = "boot: xhci kbd-score=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gKbdParseScore % 10));
        P = " mouse-score=";
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + (gMouseParseScore % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    return 1;
}

/*
 * v8：G102 等被 skip-as-kbd 时，slot 已 Address 在 gDevCtx/gEp0。
 * 勿 Disable+再 Address（真机常 cc=0x04 / Reset 超时 → m=0）。
 * 直接把当前 slot 认领为独立鼠标。
 */
static int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                                     UINT8 ConfigVal) {
    UINT8 Iface = 0, EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT32 Slot;

    if (HalCpuIsHypervisor() || gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }

    Slot = gSlotId;
    CopyMemory(gMouseDevCtx, gDevCtx, sizeof(gMouseDevCtx));
    FlushDma(gMouseDevCtx, sizeof(gMouseDevCtx));
    DcbaaSet(Slot, PointerToPhysical(gMouseDevCtx));
    DcbaaFlush();

    gMouseSlotId = Slot;
    gMousePort = RootPort;
    gMouseRoute = gKbdRoute & 0xFFFFFu;
    gMouseHubSlot = gKbdHubSlot;
    gMouseTtPort = gKbdTtPort;
    gMouseIface = Iface;
    gMouseAbsolute = 0;
    /* gSlotEp0UsesKbdRing[Slot] 保持 1：EP0 仍用 Address 时的 gEp0 */

    gSlotId = 0;
    gIntrDci = 0;
    gKbdIface = 0;
    gKbdRoute = 0;
    gKbdHubSlot = 0;
    gKbdTtPort = 0;

    gXferSlot = gMouseSlotId;
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("boot: xhci mouse claim SetConfig fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2) {
        (void)SetProtocolBoot(Iface);
    }
    if (gMouseIfaceProto == 1 || gMouseIfaceProto == 2 || gMouseIfaceProto == 0xFF) {
        SetIdle(Iface);
    }
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        BootLog("boot: xhci mouse claim ConfigEP fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    BootLogHexV("boot: xhci mouse claim score=", gMouseParseScore, 2);
    BootLog("boot: xhci-hid mouse (claim after skip-kbd)\n");
    return 1;
}

static int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundIface = 0;
    UINT8 BestScore = 0;
    UINT8 BestIface = 0;
    UINT8 BestEp = 0;
    UINT16 BestMps = 8;
    UINT8 BestInterval = 10;
    UINT8 BestProto = 0xFF;
    UINT8 CurProto = 0;
    UINT8 CurSub = 0;
    UINT8 CurScore = 0;

    *Iface = 0;
    *EpAddr = 0;
    *Mps = 8;
    *Interval = 10;

    /*
     * 评分：boot mouse (3/1/2)=3；boot 子类 Proto0 (3/1/0)=2；其它 HID 非键盘=1。
     * 真机曾把 Proto=0 的附加 HID（媒体键等）当成鼠标 → EP 无报告 m=0。
     */
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Class = Cfg[Off + 5];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            CurScore = 0;
            FoundIface = 0;
            if (Class == 3 && CurProto != 1) {
                FoundIface = 1;
                *Iface = Cfg[Off + 2];
                if (CurSub == 1 && CurProto == 2) {
                    CurScore = 3;
                } else if (CurSub == 1) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5 && Len >= 7 && FoundIface && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03) && CurScore > BestScore) {
                BestScore = CurScore;
                BestProto = CurProto;
                BestIface = *Iface;
                BestEp = Addr;
                BestMps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                BestInterval = Cfg[Off + 6];
                if (BestScore == 3) {
                    break;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    (void)Speed;
    if (BestScore == 0) {
        return 0;
    }
    *Iface = BestIface;
    *EpAddr = BestEp;
    *Mps = BestMps;
    *Interval = BestInterval;
    gMouseIfaceProto = BestProto;
    gMouseParseScore = BestScore;
    gMouseAbsolute = (BestProto != 2 && HalCpuIsHypervisor()) ? 1 : 0;
    return 1;
}

static int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                              UINT8 Speed) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    UINT32 CtxEntries;
    UINT32 Route = 0;
    UINT32 RootPort;
    UINT8 HubSlot = 0;
    UINT8 TtPort = 0;
    int Composite;
    int AddOnly;

    gMouseIntrDci = (UINT32)EpNum * 2 + In;
    gMouseEpAddr = EpAddr;
    if (Mps == 0 || Mps > 64) {
        Mps = 8;
    }
    /* 与键盘一致：TRB 长度用 8+ISP；gMouseReportLen 仅作解析上限 */
    gMouseReportLen = (UINT8)(Mps > 8 ? 8 : Mps);
    if (gMouseReportLen < 3) {
        gMouseReportLen = 3;
    }

    CtxEntries = gMouseIntrDci;
    RootPort = gMousePort;
    Composite = (SlotId == gSlotId && gSlotId != 0);
    AddOnly = 0;

    /*
     * 复合设备：只 Add 鼠标，勿 Drop/重建键盘 EP（Linux：各 iface 独立）。
     * NUC PHOTO：Stop+Sync 后仍 k=0、m 正常 → 不要 Stop 键盘；
     * Running 时 Add 若 ConfigEP 失败再回退 Stop+Add-only / Drop+Add。
     */
    if (Composite) {
        if (gIntrDci > CtxEntries) {
            CtxEntries = gIntrDci;
        }
        Route = gKbdRoute & 0xFFFFFu;
        RootPort = gPort1;
        HubSlot = gKbdHubSlot;
        TtPort = gKbdTtPort;
        Speed = gSpeed;
        AddOnly = 1;
    } else {
        /* 独立鼠标（含 hub 子口）：ConfigEP 必须带回 Address 时的 Route/TT */
        Route = gMouseRoute & 0xFFFFFu;
        HubSlot = gMouseHubSlot;
        TtPort = gMouseTtPort;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    if (Composite && gIntrDci != 0 && AddOnly) {
        /* 不 Drop 键盘；只 Add slot + 鼠标 DCI，抬高 Context Entries */
        *(UINT32 *)(void *)(gInCtx + 0) = 0;
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    } else if (Composite && gIntrDci != 0) {
        *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
        *(UINT32 *)(void *)(gInCtx + 4) =
            (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
    } else {
        *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
    }

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
    Slot[1] = (UINT32)RootPort << 16;
    if (HubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
    }

    /* Drop+Add 回退路径才重填键盘 EP；Add-only 保持首次 ConfigureIntr 的上下文 */
    if (Composite && gIntrDci != 0 && !AddOnly) {
        UINT32 *KbdEp = (UINT32 *)(void *)InEp(gIntrDci);
        UINT8 KbdIv;
        UINT16 KbdMps;
        UINT64 KbdDeq;

        KbdMps = gKbdMps;
        if (KbdMps == 0 || KbdMps > 64) {
            KbdMps = 8;
        }
        KbdIv = gKbdEpInterval;
        if (KbdIv == 0) {
            KbdIv = (Speed >= 3) ? 3 : FsInterval(10);
        }
        InitRing(gIntrRing, &gIntr, RING_SIZE);
        KbdEp[0] = (UINT32)KbdIv << 16;
        KbdEp[1] = (3u << 1) | (7u << 3) | ((UINT32)KbdMps << 16);
        KbdDeq = PointerToPhysical(gIntrRing) | 1;
        KbdEp[2] = (UINT32)KbdDeq;
        KbdEp[3] = (UINT32)(KbdDeq >> 32);
        KbdEp[4] = (UINT32)KbdMps | ((UINT32)KbdMps << 16);
        FlushDma(gIntrRing, sizeof(gIntrRing));
    }

    InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0)
                                  : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        if (Composite && AddOnly && gIntrDci != 0) {
            UINT32 EpField = (gIntrDci & 0x1Fu) << 16;

            /*
             * Running 时 Add 失败：再试 Stop 后 Add-only（旧 NUC 经验）。
             * 仍失败才 Drop+Add（保鼠标，键盘可能 k=0）。
             */
            BootLog("boot: xhci mouse add-run fail, try stop+add\n");
            (void)Command(0, TRB_TYPE(TRB_STOP_EP) | TRB_SLOT(SlotId) | EpField, 0);
            ProcessEvents();
            if (!HalCpuIsHypervisor()) {
                ProcessEventsRealPc();
            }
            ZeroMemory(gInCtx, sizeof(gInCtx));
            *(UINT32 *)(void *)(gInCtx + 0) = 0;
            *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);
            Slot = (UINT32 *)(void *)InSlot();
            Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
            Slot[1] = (UINT32)RootPort << 16;
            if (HubSlot != 0 && Speed < 3) {
                Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
            }
            InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
            Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
            Ep[0] = (UINT32)Interval << 16;
            Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
            Deq = PointerToPhysical(gMouseIntrRing) | 1;
            Ep[2] = (UINT32)Deq;
            Ep[3] = (UINT32)(Deq >> 32);
            Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);
            FlushDma(gInCtx, sizeof(gInCtx));
            FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
            if (Command(PointerToPhysical(gInCtx),
                        TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) == 0) {
                BootLog("boot: xhci mouse stop+add ok\n");
                /* 键盘曾 Stop：调用方须 Sync+Queue */
                return 2;
            }
            BootLog("boot: xhci mouse stop+add fail, drop-add\n");
            AddOnly = 0;
            ZeroMemory(gInCtx, sizeof(gInCtx));
            *(UINT32 *)(void *)(gInCtx + 0) = (1u << gIntrDci) | (1u << gMouseIntrDci);
            *(UINT32 *)(void *)(gInCtx + 4) =
                (1u << 0) | (1u << gIntrDci) | (1u << gMouseIntrDci);
            Slot = (UINT32 *)(void *)InSlot();
            Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | Route;
            Slot[1] = (UINT32)RootPort << 16;
            if (HubSlot != 0 && Speed < 3) {
                Slot[2] = (UINT32)HubSlot | ((UINT32)TtPort << 8);
            }
            {
                UINT32 *KbdEp = (UINT32 *)(void *)InEp(gIntrDci);
                UINT8 KbdIv = gKbdEpInterval ? gKbdEpInterval
                                             : (UINT8)((Speed >= 3) ? 3 : FsInterval(10));
                UINT16 KbdMps = (gKbdMps && gKbdMps <= 64) ? gKbdMps : 8;
                UINT64 KbdDeq;

                InitRing(gIntrRing, &gIntr, RING_SIZE);
                KbdEp[0] = (UINT32)KbdIv << 16;
                KbdEp[1] = (3u << 1) | (7u << 3) | ((UINT32)KbdMps << 16);
                KbdDeq = PointerToPhysical(gIntrRing) | 1;
                KbdEp[2] = (UINT32)KbdDeq;
                KbdEp[3] = (UINT32)(KbdDeq >> 32);
                KbdEp[4] = (UINT32)KbdMps | ((UINT32)KbdMps << 16);
                FlushDma(gIntrRing, sizeof(gIntrRing));
            }
            InitRing(gMouseIntrRing, &gMouseIntr, RING_SIZE);
            Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
            Ep[0] = (UINT32)Interval << 16;
            Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
            Deq = PointerToPhysical(gMouseIntrRing) | 1;
            Ep[2] = (UINT32)Deq;
            Ep[3] = (UINT32)(Deq >> 32);
            Ep[4] = (UINT32)Mps | ((UINT32)Mps << 16);
            FlushDma(gInCtx, sizeof(gInCtx));
            FlushDma(gMouseIntrRing, sizeof(gMouseIntrRing));
            if (Command(PointerToPhysical(gInCtx),
                        TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
                DebugWrite("XHCI: mouse endpoint failed\n");
                return 0;
            }
            return 3; /* drop-add：调用方 Sync 键盘 */
        }
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    if (Composite && AddOnly) {
        BootLog("boot: xhci mouse add-only ok\n");
        return 1; /* 键盘未 Stop：勿 Sync */
    }
    return 1;
}

static void QueueMouseIntr(void) {
    UINT32 Len;

    gMouseIntrDone = 0;
    gMouseReportReady = 0;
    /* 短包只写前 N 字节；不清零会让相对鼠被「高字节非0」启发式误判成绝对 */
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    FlushDma(gMouseBuf, sizeof(gMouseBuf));
    /*
     * TRB 长度不得超过 EP MPS。真机 composite 鼠 mps=4 时曾固定 enqueue 8，
     * HC 不调度完成 → PHOTO m=0 而键盘正常。
     */
    Len = gMouseReportLen;
    if (Len == 0 || Len > 8) {
        Len = 8;
    }
    Enqueue(gMouseIntrRing, &gMouseIntr, PointerToPhysical(gMouseBuf), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(gMouseSlotId, gMouseIntrDci);
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

#define HUB_PORT_CONNECTION   (1u << 0)
#define HUB_PORT_ENABLE       (1u << 1)
#define HUB_PORT_RESET        (1u << 4)
#define HUB_PORT_POWER        (1u << 8)
#define HUB_C_PORT_CONNECTION (1u << 16)
#define HUB_C_PORT_RESET      (1u << 20)
#define HUB_FEAT_PORT_RESET   4
#define HUB_FEAT_PORT_POWER   8
#define HUB_FEAT_C_PORT_CONNECTION 16
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

static int FinishHubSetup(UINT8 *OutNumPorts) {
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
static int EnumHubChildrenForMouse(void) {
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
static int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot) {
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
static int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed) {
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

/* HID SET_INTERFACE：激活指定 Alternate（复合键鼠偶见鼠标在 alt>0） */
static int SetInterface(UINT8 Iface, UINT8 Alt) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x01,
        .bRequest = 0x0B,
        .wValue = Alt,
        .wIndex = Iface,
        .wLength = 0
    };
    return ControlXfer(&Setup, 0);
}

/*
 * 真机常见：USB 键鼠复合设备（同一 slot 上键盘 Proto=1 + 鼠标 Proto=2）。
 * 旧逻辑只扫「其它根口」，同口第二接口永远绑不上 → arms mouse=00/00。
 */
static int InitMouseOnKeyboardSlot(void) {
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

static int InitMouseOnPort(UINT32 Port1) {
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
static void ImClearPending(void) {
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
void XhciDiagFormat(char *Buf, int Max) {
    char Dig[12];
    int N = 0;
    UINT32 V[9];
    int vi;
    const char *Tags = "tikmucrdq";
    const char *Mode;

    if (!Buf || Max < 8) {
        return;
    }
    if (gIrqMode == XHCI_IRQ_MODE_DUAL) {
        Mode = "mode=dual irq=msi";
    } else if (gIrqMode == XHCI_IRQ_MODE_IRQ) {
        Mode = "mode=irq irq=msi";
    } else {
        Mode = "mode=poll";
    }
    while (*Mode && N + 1 < Max) {
        Buf[N++] = *Mode++;
    }
    V[0] = gStatXferAny;
    V[1] = gStatIntrEvt + gStatMouseEvt;
    V[2] = gStatKbdPush;
    V[3] = gStatMousePush;
    V[4] = gStatUnmatched;
    V[5] = gStatLastCc;
    V[6] = gStatEvtRing;
    V[7] = gStatDrain;
    V[8] = gStatIrq;
    Buf[N] = 0;
    for (vi = 0; vi < 9 && N + 14 < Max; vi++) {
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
    /*
     * 一眼读相：鼠有键无 + s 落在鼠 DCI → 键中断 IN 没完成（不是「计数器坏了」）。
     * 期望键 DCI 常为 05；s=05.03 只说明最近事件是鼠标。
     */
    if (V[2] == 0 && V[3] > 0 && N + 18 < Max) {
        const char *H = " !kbdIN=0";
        while (*H && N + 1 < Max) {
            Buf[N++] = *H++;
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
    BootLog(Line); /* 真机 PHOTO 可见 slot/DCI，对照 s= */
}

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
