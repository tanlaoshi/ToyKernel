/*
 * XHCI.c — xHCI 主机控制器与 USB 键盘驱动
 *
 * 实现命令环/事件环、Enable Slot、Address Device、EP0 控制传输、
 * 中断端点 IN 轮询。键盘报告入队后由 XHCIDequeueKeyboard 取出。
 *
 * 主要静态辅助函数：
 *   Rd32/Wr32/Wr64/Phys/Fence/Zero — MMIO 与内存工具
 *   InitRing/Enqueue/ProcessEvents — TRB 环管理
 *   ResetController/StartController — 控制器生命周期
 *   ResetPort/AddressDevice — 端口与设备枚举
 *   ControlXfer/GetDesc/SetConfig — USB 控制传输
 *   ConfigureIntr/QueueIntr/ParseConfig — HID 中断端点
 *
 * 对外 API：
 *   XHCIInit64        — 完整初始化并枚举键盘
 *   XHCIEnableIrq     — 配置 MSI-X 并排空挂起事件
 *   XHCIIrq           — 中断服务例程
 *   XHCIDequeueKeyboard — 从软件队列取键盘报告
 */
#include "XHCI.h"
#include "Console.h"
#include "hal.h"

#define RING_SIZE           32
#define EVT_SIZE            32
#define DCBAA_SLOTS         16
#define PORTSC_CCS          (1u << 0)
#define PORTSC_PED          (1u << 1)
#define PORTSC_PR           (1u << 4)
#define PORTSC_PP           (1u << 9)
#define PORTSC_CSC          (1u << 17)
#define PORTSC_PRC          (1u << 21)
#define PORTSC_CHANGE       0x00FE0000u

#define USBCMD_RS           (1u << 0)
#define USBCMD_HCRST        (1u << 1)
#define USBCMD_INTE         (1u << 2)
#define USBSTS_HCH          (1u << 0)
#define USBSTS_EINT         (1u << 2)
#define USBSTS_CNR          (1u << 6)

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
#define TRB_ADDRESS_DEV    11
#define TRB_CONFIG_EP       12
#define TRB_TRANSFER_EVENT 32
#define TRB_CMD_COMPLETION  33

#define CC_SUCCESS          1
#define CC_SHORT_PACKET     13

typedef struct {
    UINT64 Parameter;
    UINT32 Status;
    UINT32 Control;
} __attribute__((packed, aligned(16))) XHCI_TRB;

typedef struct {
    UINT32 Enq;
    UINT32 Pcs;
} RING_STATE;

static UINT64 gCap;
static UINT64 gOp;
static UINT64 gDb;
static UINT64 gRt;
static UINT32 gCtxSize;
static UINT32 gMaxPorts;
static UINT32 gPort1;
static UINT8  gSpeed;
static UINT32 gSlotId;
static UINT32 gXferSlot;
static UINT32 gIntrDci;
static UINT16 gEp0Mps;
static UINT8  gKbdIface;
static UINT8  gUseGetReport;
static UINT8  gUseIrq;

#define KBD_Q 16
static USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
static volatile UINT32 gKbdW;
static volatile UINT32 gKbdR;

static UINT32 gMouseSlotId;
static UINT32 gMousePort;
static UINT32 gMouseIntrDci;
static UINT8  gMouseReportLen;
static UINT8  gMouseBuf[8];
static XHCI_TRB gMouseIntrRing[RING_SIZE];
static RING_STATE gMouseIntr;
static UINT8  gMouseDevCtx[2048];
static volatile UINT32 gMouseIntrDone;

#define MOUSE_Q 32
static USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
static volatile UINT32 gMouseW;
static volatile UINT32 gMouseR;

static XHCI_TRB gCmdRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEp0Ring[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gIntrRing[RING_SIZE] __attribute__((aligned(64)));
static XHCI_TRB gEvtRing[EVT_SIZE] __attribute__((aligned(64)));

static RING_STATE gCmd;
static RING_STATE gEp0;
static RING_STATE gIntr;
static UINT32 gEvtDeq;
static UINT32 gEvtCcs;

static UINT64 gDcbaa[DCBAA_SLOTS + 1] __attribute__((aligned(64)));
static UINT64 gScratchPtr[16] __attribute__((aligned(64)));
static UINT8  gScratchBuf[8][4096] __attribute__((aligned(4096)));
static UINT8  gDevCtx[2048] __attribute__((aligned(64)));
static UINT8  gInCtx[2048] __attribute__((aligned(64)));
static UINT8  gCtrlBuf[256] __attribute__((aligned(64)));
static UINT8  gReportBuf[8] __attribute__((aligned(64)));
static UINT8  gErst[16] __attribute__((aligned(64)));

static volatile UINT32 gCmdDone;
static UINT32 gCmdCode;
static UINT32 gCmdSlot;
static volatile UINT32 gXferDone;
static UINT32 gXferCode;
static UINT32 gXferRemain;
static volatile UINT32 gIntrDone;

/* 读 MMIO 32 位 */
static inline UINT32 Rd32(UINT64 Addr) {
    return *(volatile UINT32 *)(UINTN)Addr;
}

/* 写 MMIO 32 位 */
static inline void Wr32(UINT64 Addr, UINT32 Value) {
    *(volatile UINT32 *)(UINTN)Addr = Value;
}

/* 写 MMIO 64 位（分两次 32 位写） */
static void Wr64(UINT64 Addr, UINT64 Value) {
    Wr32(Addr, (UINT32)Value);
    Wr32(Addr + 4, (UINT32)(Value >> 32));
}

/* 虚拟地址转物理地址（恒等映射） */
static UINT64 Phys(const void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}

/* 内存屏障，保证 TRB 写入对硬件可见 */
static void Fence(void) {
    __asm__ volatile ("mfence" ::: "memory");
}

/* 清零内存块 */
static void Zero(void *Ptr, UINTN Size) {
    UINT8 *P = (UINT8 *)Ptr;
    while (Size--) {
        *P++ = 0;
    }
}

/* 等待寄存器 Mask 位清零 */
static int WaitClear(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (!(Rd32(Addr) & Mask)) {
            return 1;
        }
    }
    return 0;
}

/* 等待寄存器 Mask 位置位 */
static int WaitSet(UINT64 Addr, UINT32 Mask, int Timeout) {
    while (Timeout--) {
        if (Rd32(Addr) & Mask) {
            return 1;
        }
    }
    return 0;
}

/* 初始化 TRB 环状态 */
static void InitRing(XHCI_TRB *Ring, RING_STATE *St) {
    Zero(Ring, sizeof(XHCI_TRB) * RING_SIZE);
    Ring[RING_SIZE - 1].Parameter = Phys(&Ring[0]);
    Ring[RING_SIZE - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | TRB_C;
    St->Enq = 0;
    St->Pcs = 1;
}

/* 向环尾入队一条 TRB */
static void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control) {
    UINT32 i = St->Enq;
    Ring[i].Parameter = Param;
    Ring[i].Status = Status;
    Fence();
    Ring[i].Control = Control | (St->Pcs & 1);
    i++;
    if (i == RING_SIZE - 1) {
        Ring[RING_SIZE - 1].Parameter = Phys(&Ring[0]);
        Ring[RING_SIZE - 1].Control = TRB_TYPE(TRB_LINK) | TRB_TC | (St->Pcs & 1);
        i = 0;
        St->Pcs ^= 1;
    }
    St->Enq = i;
}

/* 从 TRB Control 字段提取类型 */
static UINT32 TrbType(UINT32 Control) {
    return (Control >> 10) & 0x3F;
}

/* 处理事件环中所有待处理 TRB（命令完成、传输完成） */
static void ProcessEvents(void) {
    for (;;) {
        XHCI_TRB *Evt = &gEvtRing[gEvtDeq];
        if ((Evt->Control & TRB_C) != gEvtCcs) {
            break;
        }

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
            gXferCode = Code;
            gXferRemain = Evt->Status & 0xFFFFFF;
            gXferDone = 1;
            if (EvtSlot == gSlotId &&
                (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) &&
                (Ep == gIntrDci || Ep == 1 || Ep == 2)) {
                gIntrDone = 1;
            }
            if (gMouseSlotId && EvtSlot == gMouseSlotId &&
                (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) &&
                (Ep == gMouseIntrDci || Ep == 3 || Ep == 4 || Ep == 5)) {
                gMouseIntrDone = 1;
            }
        }

        gEvtDeq++;
        if (gEvtDeq == EVT_SIZE) {
            gEvtDeq = 0;
            gEvtCcs ^= 1;
        }
    }

    UINT64 Erdp = Phys(&gEvtRing[gEvtDeq]) | (1ULL << 3);
    Wr64(gRt + 0x38, Erdp);
    Wr32(gOp + 4, USBSTS_EINT);
}

/* 等待命令环完成事件 */
static int WaitCmd(int Timeout) {
    while (Timeout--) {
        ProcessEvents();
        if (gCmdDone) {
            return (gCmdCode == CC_SUCCESS) ? 0 : -1;
        }
    }
    return -1;
}

/* 等待传输环完成事件 */
static int WaitXfer(int Timeout) {
    while (Timeout--) {
        ProcessEvents();
        if (gXferDone) {
            return (gXferCode == CC_SUCCESS || gXferCode == CC_SHORT_PACKET) ? 0 : -1;
        }
    }
    return -1;
}

/* 敲 Doorbell 通知硬件处理环 */
static void RingDb(UINT32 Slot, UINT32 Target) {
    Fence();
    Wr32(gDb + Slot * 4, Target & 0xFF);
}

/* 提交一条命令 TRB 并等待完成 */
static int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    gCmdDone = 0;
    Enqueue(gCmdRing, &gCmd, Param, 0, Control);
    RingDb(0, 0);
    if (WaitCmd(2000000) < 0) {
        ConsoleWrite("XHCI: command timeout/fail cc=");
        ConsoleHex32(gCmdCode);
        ConsoleWrite("\n");
        return -1;
    }
    if (SlotOut) {
        *SlotOut = gCmdSlot;
    }
    return 0;
}

static UINT8 *InSlot(void) {
    return gInCtx + gCtxSize;
}

static UINT8 *InEp(UINT32 Dci) {
    return gInCtx + gCtxSize * (Dci + 1);
}

/* 释放 USB 传统支持（BIOS 移交） */
static void TakeLegacy(void) {
    UINT32 Hcc1 = Rd32(gCap + 0x10);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFF;
    if (Xecp == 0) {
        return;
    }
    UINT64 Ptr = gCap + (UINT64)Xecp * 4;
    for (int i = 0; i < 64; i++) {
        UINT32 Val = Rd32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFF);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFF);
        if (Id == 1) {
            Wr32(Ptr, Val | (1u << 24));
            WaitClear(Ptr, (1u << 16), 1000000);
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCap + (UINT64)Next * 4;
    }
}

/* 复位 xHCI 控制器 */
static int ResetController(void) {
    UINT32 Cmd = Rd32(gOp);
    Cmd &= ~USBCMD_RS;
    Wr32(gOp, Cmd);
    if (!WaitSet(gOp + 4, USBSTS_HCH, 1000000)) {
        ConsoleWrite("XHCI: halt timeout\n");
        return 0;
    }
    Wr32(gOp, USBCMD_HCRST);
    if (!WaitClear(gOp, USBCMD_HCRST, 1000000) || !WaitClear(gOp + 4, USBSTS_CNR, 1000000)) {
        ConsoleWrite("XHCI: reset timeout\n");
        return 0;
    }
    return 1;
}

/* 分配 DCBAA、建环并 Run 控制器 */
static int StartController(UINT32 MaxSlots) {
    UINT32 Hcs2 = Rd32(gCap + 0x08);
    UINT32 Scratch = ((Hcs2 >> 21) & 0x1F) | (((Hcs2 >> 27) & 0x1F) << 5);

    Zero(gDcbaa, sizeof(gDcbaa));
    Zero(gDevCtx, sizeof(gDevCtx));
    if (Scratch > 0) {
        if (Scratch > 8) {
            ConsoleWrite("XHCI: too many scratchpad buffers\n");
            return 0;
        }
        Zero(gScratchPtr, sizeof(gScratchPtr));
        for (UINT32 i = 0; i < Scratch; i++) {
            Zero(gScratchBuf[i], 4096);
            gScratchPtr[i] = Phys(gScratchBuf[i]);
        }
        gDcbaa[0] = Phys(gScratchPtr);
    }

    Wr32(gOp + 0x38, MaxSlots);
    Wr64(gOp + 0x30, Phys(gDcbaa));

    InitRing(gCmdRing, &gCmd);
    Wr64(gOp + 0x18, Phys(gCmdRing) | 1);

    Zero(gEvtRing, sizeof(gEvtRing));
    gEvtDeq = 0;
    gEvtCcs = 1;
    Zero(gErst, sizeof(gErst));
    *(UINT64 *)(void *)gErst = Phys(gEvtRing);
    *(UINT16 *)(void *)(gErst + 8) = EVT_SIZE;

    Wr32(gRt + 0x20, 3);
    Wr32(gRt + 0x24, 0);
    Wr32(gRt + 0x28, 1);
    Wr32(gRt + 0x2C, 0);
    Wr64(gRt + 0x30, Phys(gErst));
    Wr64(gRt + 0x38, Phys(gEvtRing) | (1ULL << 3));

    Wr32(gOp, USBCMD_RS | USBCMD_INTE);
    if (!WaitClear(gOp + 4, USBSTS_HCH, 1000000)) {
        ConsoleWrite("XHCI: run timeout\n");
        return 0;
    }
    return 1;
}

static UINT32 PortReg(UINT32 Port1) {
    return 0x400 + (Port1 - 1) * 0x10;
}

/* 复位指定端口并等待连接使能 */
static int ResetPort(UINT32 Port1) {
    UINT64 Ps = gOp + PortReg(Port1);
    UINT32 Val = Rd32(Ps);
    if (!(Val & PORTSC_CCS)) {
        return 0;
    }

    if (!(Val & PORTSC_PP)) {
        Wr32(Ps, (Val & ~PORTSC_CHANGE) | PORTSC_PP);
        WaitSet(Ps, PORTSC_PP, 100000);
        Val = Rd32(Ps);
    }

    Wr32(Ps, (Val & ~PORTSC_CHANGE) | PORTSC_PR | PORTSC_PP);
    if (!WaitSet(Ps, PORTSC_PRC, 2000000)) {
        ConsoleWrite("XHCI: port reset timeout\n");
        return 0;
    }

    Val = Rd32(Ps);
    Wr32(Ps, (Val & ~PORTSC_CHANGE) | PORTSC_PRC | PORTSC_CSC);
    for (int t = 0; t < 100000; t++) {
        Val = Rd32(Ps);
        if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
            return 1;
        }
    }
    ConsoleWrite("XHCI: port not enabled after reset\n");
    return 0;
}

static UINT8 PortSpeed(UINT32 Portsc) {
    return (UINT8)((Portsc >> 10) & 0xF);
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

static int AddressDeviceOnPort(UINT32 Port1, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx) {
    gXferSlot = 0;
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), SlotOut) < 0 || *SlotOut == 0 ||
        *SlotOut > DCBAA_SLOTS) {
        ConsoleWrite("XHCI: Enable Slot failed\n");
        return 0;
    }
    ConsoleWrite("XHCI: Slot ");
    ConsoleHex32(*SlotOut);
    ConsoleWrite("\n");

    gXferSlot = *SlotOut;
    gDcbaa[*SlotOut] = Phys(DevCtx);
    Zero(DevCtx, 2048);
    Zero(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)Port1 << 16;

    InitRing(gEp0Ring, &gEp0);
    UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
    gEp0Mps = SpeedMps(Speed);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEp0Mps << 16);
    UINT64 Deq = Phys(gEp0Ring) | 1;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    if (Command(Phys(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(*SlotOut), 0) < 0) {
        ConsoleWrite("XHCI: Address Device failed\n");
        return 0;
    }
    ConsoleWrite("XHCI: Address Device OK\n");
    return 1;
}

/* Enable Slot + Address Device 命令序列 */
static int AddressDevice(UINT32 Port1, UINT8 Speed) {
    gXferSlot = gSlotId;
    return AddressDeviceOnPort(Port1, Speed, &gSlotId, gDevCtx);
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
        Enqueue(gEp0Ring, &gEp0, Phys(Data), Setup->wLength, TRB_TYPE(TRB_DATA) | Dir);
    }

    UINT32 StatusDir = (Setup->wLength && (Setup->bmRequestType & 0x80)) ? 0 : TRB_DIR_IN;
    Enqueue(gEp0Ring, &gEp0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | StatusDir);
    RingDb(gXferSlot, 1);
    if (WaitXfer(2000000) < 0) {
        ConsoleWrite("XHCI: EP0 transfer failed cc=");
        ConsoleHex32(gXferCode);
        ConsoleWrite("\n");
        return -1;
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
    Zero(Buf, Length);
    return ControlXfer(&Setup, Buf);
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

    Zero(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gIntrDci);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = ((UINT32)gIntrDci << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)gPort1 << 16;

    InitRing(gIntrRing, &gIntr);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = Phys(gIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    if (Command(Phys(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        ConsoleWrite("XHCI: Configure Endpoint failed\n");
        return 0;
    }
    ConsoleWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/* 提交中断 IN 传输 TRB */
static void QueueIntr(void) {
    gIntrDone = 0;
    Enqueue(gIntrRing, &gIntr, Phys(gReportBuf), 8, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    RingDb(gSlotId, gIntrDci);
}

static int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                       UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundIface = 0;
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
            if (Class == 3 && Sub == 1 && Proto == 1) {
                FoundIface = 1;
                *Iface = Cfg[Off + 2];
            } else {
                FoundIface = 0;
            }
        } else if (Type == 5 && Len >= 7 && FoundIface) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03)) {
                *EpAddr = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                *Interval = Cfg[Off + 6];
                (void)Speed;
                return 1;
            }
        }
        Off = (UINT16)(Off + Len);
    }
    return 0;
}

static int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                            UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval) {
    UINT16 Off = 0;
    UINT8 FoundIface = 0;
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
            if (Class == 3) {
                FoundIface = 1;
                *Iface = Cfg[Off + 2];
            } else {
                FoundIface = 0;
            }
        } else if (Type == 5 && Len >= 7 && FoundIface) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80) && ((Attr & 0x03) == 0x03)) {
                *EpAddr = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
                *Interval = Cfg[Off + 6];
                (void)Speed;
                return 1;
            }
        }
        Off = (UINT16)(Off + Len);
    }
    return 0;
}

static int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                              UINT8 Speed) {
    UINT8 EpNum = EpAddr & 0x0F;
    UINT8 In = (EpAddr & 0x80) ? 1 : 0;
    gMouseIntrDci = (UINT32)EpNum * 2 + In;
    gMouseReportLen = (UINT8)(Mps > 8 ? 8 : Mps);

    Zero(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gMouseIntrDci);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = ((UINT32)gMouseIntrDci << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)gMousePort << 16;

    InitRing(gMouseIntrRing, &gMouseIntr);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gMouseIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0)
                                  : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = Phys(gMouseIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    if (Command(Phys(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        ConsoleWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    return 1;
}

static void QueueMouseIntr(void) {
    gMouseIntrDone = 0;
    Enqueue(gMouseIntrRing, &gMouseIntr, Phys(gMouseBuf), gMouseReportLen,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    RingDb(gMouseSlotId, gMouseIntrDci);
}

static int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                          int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                         UINT16 *, UINT8 *),
                          int UseBootProto) {
    gXferSlot = SlotId;
    (void)DevCtx;

    for (volatile int d = 0; d < 500000; d++) {
    }

    InitRing(gEp0Ring, &gEp0);

    if (GetDesc(0x0100, 0, 18, gCtrlBuf) < 0) {
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

static int InitMouseOnPort(UINT32 Port1) {
    UINT32 Ps = Rd32(gOp + PortReg(Port1));
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    if (!ResetPort(Port1)) {
        return 0;
    }
    UINT8 Speed = PortSpeed(Rd32(gOp + PortReg(Port1)));
    gMousePort = Port1;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx)) {
        gMouseSlotId = 0;
        return 0;
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 0)) {
        ConsoleWrite("XHCI: mouse config failed\n");
        gMouseSlotId = 0;
        return 0;
    }

    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 Iface = 0;
    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
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
        gMouseSlotId = 0;
        return 0;
    }
    if (!ParseConfigMouse(gCtrlBuf, Total, Speed, &Iface, &EpAddr, &Mps, &Interval)) {
        ConsoleWrite("XHCI: mouse no interrupt EP\n");
        gMouseSlotId = 0;
        return 0;
    }
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        ConsoleWrite("XHCI: mouse endpoint failed\n");
        gMouseSlotId = 0;
        return 0;
    }
    Zero(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    ConsoleWrite("XHCI: mouse ready\n");
    return 1;
}

/* 完整 xHCI 初始化：复位、建环、枚举端口上的 USB 键盘 */
int XHCIInit64(UINT64 BaseAddress) {
    if (BaseAddress == 0) {
        ConsoleWrite("XHCI: null BAR\n");
        return 0;
    }

    gCap = BaseAddress;
    UINT32 Cap = Rd32(gCap);
    UINT32 CapLength = Cap & 0xFF;
    if (CapLength < 0x20) {
        ConsoleWrite("XHCI: bad CapLength ");
        ConsoleHex32(Cap);
        ConsoleWrite("\n");
        return 0;
    }

    gOp = gCap + CapLength;
    gDb = gCap + (Rd32(gCap + 0x14) & ~0x3u);
    gRt = gCap + (Rd32(gCap + 0x18) & ~0x1Fu);
    gCtxSize = (Rd32(gCap + 0x10) & (1u << 2)) ? 64 : 32;

    UINT32 Hcs1 = Rd32(gCap + 0x04);
    UINT32 MaxSlots = Hcs1 & 0xFF;
    gMaxPorts = (Hcs1 >> 24) & 0xFF;
    if (MaxSlots == 0) {
        MaxSlots = 1;
    }
    if (MaxSlots > DCBAA_SLOTS) {
        MaxSlots = DCBAA_SLOTS;
    }

    ConsoleWrite("XHCI: cap=");
    ConsoleHex64(gCap);
    ConsoleWrite(" ports=");
    ConsoleHex32(gMaxPorts);
    ConsoleWrite(" ctx=");
    ConsoleHex32(gCtxSize);
    ConsoleWrite("\n");

    TakeLegacy();
    if (!ResetController() || !StartController(MaxSlots)) {
        return 0;
    }
    ConsoleWrite("XHCI: controller running\n");

    UINT32 Port1 = 0;
    UINT8 Speed = 0;
    for (int Wait = 0; Wait < 50 && Port1 == 0; Wait++) {
        for (UINT32 p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT32 Ps = Rd32(gOp + PortReg(p));
            if (Ps & PORTSC_CCS) {
                ConsoleWrite("XHCI: device on port ");
                ConsoleHex32(p);
                ConsoleWrite(" portsc=");
                ConsoleHex32(Ps);
                ConsoleWrite("\n");
                if (ResetPort(p)) {
                    UINT32 After = Rd32(gOp + PortReg(p));
                    Speed = PortSpeed(After);
                    Port1 = p;
                    gPort1 = p;
                    gSpeed = Speed;
                    ConsoleWrite("XHCI: speed=");
                    ConsoleHex32(Speed);
                    ConsoleWrite("\n");
                    break;
                }
            }
        }
        if (Port1 == 0) {
            for (volatile int d = 0; d < 200000; d++) {
            }
        }
    }
    if (Port1 == 0) {
        ConsoleWrite("XHCI: no device\n");
        return 0;
    }

    if (!AddressDevice(Port1, Speed)) {
        return 0;
    }

    if (GetDesc(0x0100, 0, 18, gCtrlBuf) < 0) {
        ConsoleWrite("XHCI: GET_DESCRIPTOR device failed\n");
        return 0;
    }
    USB_DEVICE_DESCRIPTOR *Dev = (USB_DEVICE_DESCRIPTOR *)(void *)gCtrlBuf;
    ConsoleWrite("XHCI: VID=");
    ConsoleHex32(Dev->idVendor);
    ConsoleWrite(" PID=");
    ConsoleHex32(Dev->idProduct);
    ConsoleWrite("\n");

    if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
        ConsoleWrite("XHCI: GET_DESCRIPTOR config(9) failed\n");
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
        ConsoleWrite("XHCI: GET_DESCRIPTOR config failed\n");
        return 0;
    }
    UINT8 ConfigVal = gCtrlBuf[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }

    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    int HaveIntr = ParseConfig(gCtrlBuf, Total, Speed, &gKbdIface, &EpAddr, &Mps, &Interval);

    if (SetConfig(ConfigVal) < 0) {
        ConsoleWrite("XHCI: SET_CONFIGURATION failed\n");
        return 0;
    }
    if (SetProtocolBoot(gKbdIface) < 0) {
        ConsoleWrite("XHCI: SET_PROTOCOL failed (continuing)\n");
    }
    SetIdle(gKbdIface);

    gUseGetReport = 1;
    if (HaveIntr && ConfigureIntr(EpAddr, Mps, Interval, Speed)) {
        Zero(gReportBuf, 8);
        QueueIntr();
        gUseGetReport = 0;
    } else {
        ConsoleWrite("XHCI: using GET_REPORT fallback\n");
    }

    ConsoleWrite("XHCI: keyboard ready\n");

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

/* 将键盘报告推入环形软件队列 */
static void KbdPush(void) {
    UINT32 Next = (gKbdW + 1) % KBD_Q;
    if (Next == gKbdR) {
        return;
    }
    UINT8 *Dst = (UINT8 *)&gKbdQ[gKbdW];
    for (int i = 0; i < 8; i++) {
        Dst[i] = gReportBuf[i];
    }
    gKbdW = Next;
}

static void MousePush(void) {
    UINT32 Next = (gMouseW + 1) % MOUSE_Q;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;

    if (Next == gMouseR) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseW];
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    if (gMouseReportLen >= 6 && X0 <= 32767 && Y0 <= 32767) {
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
    } else if (gMouseReportLen >= 7 && X1 <= 32767 && Y1 <= 32767) {
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
    } else {
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
        R->X = (UINT32)AbsX;
        R->Y = (UINT32)AbsY;
        R->Buttons = gMouseBuf[0] & 7;
    }
    gMouseW = Next;
}

/* 清除中断管理器挂起位 */
static void ImClearPending(void) {
    UINT32 Im = Rd32(gRt + 0x20);
    Wr32(gRt + 0x20, Im | 1u);
}

/* XHCI MSI-X 中断处理：处理事件、重新排队中断传输 */
void XHCIIrq(void) {
    ProcessEvents();
    ImClearPending();
    if (gIntrDone) {
        gIntrDone = 0;
        KbdPush();
        QueueIntr();
    }
    if (gMouseIntrDone) {
        gMouseIntrDone = 0;
        MousePush();
        QueueMouseIntr();
    }
}

/* 轮询排空事件环（启用 IRQ 前调用） */
void XHCIDrainEvents(void) {
    for (int i = 0; i < 8; i++) {
        if (!(Rd32(gRt + 0x20) & 1u)) {
            break;
        }
        XHCIIrq();
    }
}

/* 通过 PciEnableMsi 绑定中断向量并启用 IRQ 模式 */
int XHCIEnableIrq(USB_CONTROLLER *Dev) {
    if (gUseGetReport) {
        ConsoleWrite("XHCI: GET_REPORT mode, IRQ unused\n");
        gUseIrq = 0;
        return 0;
    }
    if (!PciEnableMsi(Dev, VEC_XHCI)) {
        gUseIrq = 0;
        return 0;
    }
    gUseIrq = 1;
    XHCIDrainEvents();
    return 1;
}

/* 返回是否使用 MSI-X 中断模式（否则为 GET_REPORT 轮询） */
int XHCIUsesIrq(void) {
    return gUseIrq != 0;
}

/* 从键盘报告队列取一条，有数据返回 1，空队列返回 0 */
int XHCIDequeueKeyboard(USB_KEYBOARD_REPORT *Report) {
    if (gKbdR == gKbdW) {
        return 0;
    }
    UINT8 *Src = (UINT8 *)&gKbdQ[gKbdR];
    UINT8 *Dst = (UINT8 *)Report;
    for (int i = 0; i < 8; i++) {
        Dst[i] = Src[i];
    }
    gKbdR = (gKbdR + 1) % KBD_Q;
    return 1;
}

int XHCIMousePresent(void) {
    return gMouseSlotId != 0;
}

int XHCIDequeueMouse(USB_MOUSE_REPORT *Report) {
    if (gMouseR == gMouseW) {
        return 0;
    }
    *Report = gMouseQ[gMouseR];
    gMouseR = (gMouseR + 1) % MOUSE_Q;
    return 1;
}
