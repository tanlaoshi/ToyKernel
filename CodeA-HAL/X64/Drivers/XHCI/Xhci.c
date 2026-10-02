/*
 * Xhci.c — Init/Ready/Abandon 与共享全局（Keyboard/Device/Controller/Transfer/Command/Ring/Mmio 已拆）
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
volatile UINT32 gMscBotBusy; /* BOT 整段互斥（勿 SpinLock：WaitBulk 可数百 ms） */
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
volatile UINT32 gStatIrqSkipped; /* excl-1：独占窗内 IRQ 跳过环推进 */


USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
volatile UINT32 gMouseWriteIndex;
volatile UINT32 gMouseReadIndex;
SPIN_LOCK gHidQueueLock; /* PR-S-ap：IRQ 入队 vs AP 出队 */
SPIN_LOCK gEvtConsumerLock; /* excl-2：事件环消费串行 */

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
UINT32 gXhciCmdSick; /* 命令环超时未恢复：禁再发命令 */
UINT32 gXhciCmdWaiting; /* 1：WaitCommand 中，Drain/Irq 勿碰事件环 */
volatile UINT32 gXferDone;
UINT32 gXferCode;
UINT32 gXferRemain;
volatile UINT32 gIntrDone;

UINT8 *InSlot(void) {
    return gInCtx + gCtxSize;
}

UINT8 *InEp(UINT32 Dci) {
    return gInCtx + gCtxSize * (Dci + 1);
}

/* 完整 xHCI 初始化：硬件起机 → 枚举键盘 → 绑鼠标（BSS 勿迁出本文件） */
int XhciInit(UINT64 BaseAddress) {
    if (!XhciInitHw(BaseAddress)) {
        return 0;
    }
    return XhciEnumAndBind();
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
    BootLog("Boot: XHCI abandon no HID\n");
}


