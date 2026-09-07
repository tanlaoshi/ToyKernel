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
#define TRB_DISABLE_SLOT   10
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

static UINT64 gCapabilityBase;
static UINT64 gOperationalBase;
static UINT64 gDoorbellBase;
static UINT64 gRuntimeBase;
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
static volatile UINT32 gKeyboardWriteIndex;
static volatile UINT32 gKeyboardReadIndex;

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
static volatile UINT32 gMouseWriteIndex;
static volatile UINT32 gMouseReadIndex;

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
/* HCSPARAMS2 MaxScratchpadBufs 可达 1023；真机见过 0x41(=65)，留到 128 */
#define XHCI_SCRATCH_MAX 128
static UINT64 gScratchPtr[XHCI_SCRATCH_MAX] __attribute__((aligned(64)));
static UINT8  gScratchBuf[XHCI_SCRATCH_MAX][4096] __attribute__((aligned(4096)));
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

/* 初始化 TRB 环状态 */
static void InitRing(XHCI_TRB *Ring, RING_STATE *St) {
    ZeroMemory(Ring, sizeof(XHCI_TRB) * RING_SIZE);
    Ring[RING_SIZE - 1].Parameter = PointerToPhysical(&Ring[0]);
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
        Ring[RING_SIZE - 1].Parameter = PointerToPhysical(&Ring[0]);
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
    int Progress = 0;

    for (;;) {
        XHCI_TRB *Evt = &gEvtRing[gEvtDeq];
        if ((Evt->Control & TRB_C) != gEvtCcs) {
            break;
        }
        Progress = 1;

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

    /* 无事件时勿狂写 ERDP——真机 WaitCommand 空转会 MMIO 拖死 */
    if (Progress) {
        UINT64 Erdp = PointerToPhysical(&gEvtRing[gEvtDeq]) | (1ULL << 3);
        WriteMmio64(gRuntimeBase + 0x38, Erdp);
        WriteMmio32(gOperationalBase + 4, USBSTS_EINT);
    }
}

/* 等待命令环完成事件 */
static int WaitCommand(int Timeout) {
    while (Timeout--) {
        ProcessEvents();
        if (gCmdDone) {
            return (gCmdCode == CC_SUCCESS) ? 0 : -1;
        }
    }
    return -1;
}

/* 等待传输环完成事件 */
static int WaitTransfer(int Timeout) {
    if (!HalCpuIsHypervisor() && Timeout > 50000) {
        Timeout = 50000;
    }
    while (Timeout--) {
        ProcessEvents();
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

/* 提交一条命令 TRB 并等待完成 */
static int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut) {
    int Wait = HalCpuIsHypervisor() ? 150000 : 40000;

    gCmdDone = 0;
    Enqueue(gCmdRing, &gCmd, Param, 0, Control);
    RingDoorbell(0, 0);
    if (WaitCommand(Wait) < 0) {
        DebugWrite("XHCI: command timeout/fail cc=");
        DebugHex32(gCmdCode);
        DebugWrite("\n");
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
    UINT32 Hcc1 = ReadMmio32(gCapabilityBase + 0x10);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFF;
    int Wait;

    if (Xecp == 0) {
        HalSerialWrite("boot: xhci no legacy cap\n");
        return;
    }
    UINT64 Ptr = gCapabilityBase + (UINT64)Xecp * 4;
    for (int i = 0; i < 64; i++) {
        UINT32 Val = ReadMmio32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFF);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFF);
        if (Id == 1) {
            HalSerialWrite("boot: xhci legacy claim\n");
            WriteMmio32(Ptr, Val | (1u << 24));
            /* 真机 BIOS 信号量可能永不清：短等后继续，勿死等 */
            Wait = HalCpuIsHypervisor() ? 1000000 : 50000;
            if (!WaitClear(Ptr, (1u << 16), Wait)) {
                HalSerialWrite("boot: xhci legacy BIOS timeout (cont)\n");
            } else {
                HalSerialWrite("boot: xhci legacy ok\n");
            }
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCapabilityBase + (UINT64)Next * 4;
    }
    HalSerialWrite("boot: xhci legacy USBLEGSUP not found\n");
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

/* 分配 DCBAA、建环并 Run 控制器 */
static int StartController(UINT32 MaxSlots) {
    UINT32 Hcs2 = ReadMmio32(gCapabilityBase + 0x08);
    UINT32 Scratch = ((Hcs2 >> 21) & 0x1F) | (((Hcs2 >> 27) & 0x1F) << 5);
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Sts;
    char B[12];

    ZeroMemory(gDcbaa, sizeof(gDcbaa));
    ZeroMemory(gDevCtx, sizeof(gDevCtx));
    if (Scratch > 0) {
        if (Scratch > XHCI_SCRATCH_MAX) {
            BootLog("boot: xhci scratchpad >max\n");
            return 0;
        }
        {
            BootLog("boot: xhci scratchpad=");
            HalSerialFormatHex(B, Scratch, 4);
            if (RealPc) {
                HalSerialBootMark("boot: xhci scratch ok\n");
            } else {
                HalSerialWrite(B);
                HalSerialWrite("\n");
            }
        }
        ZeroMemory(gScratchPtr, sizeof(gScratchPtr));
        for (UINT32 i = 0; i < Scratch; i++) {
            gScratchPtr[i] = PointerToPhysical(gScratchBuf[i]);
            FlushDma(gScratchBuf[i], 4096);
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
    InitRing(gCmdRing, &gCmd);
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

    /* 真机：先不置 IMAN.IE；ERDP 初值勿带 EHB，减少 Run 即挂 */
    if (HalCpuIsHypervisor()) {
        WriteMmio32(gRuntimeBase + 0x20, 3);
    } else {
        WriteMmio32(gRuntimeBase + 0x20, 0);
    }
    WriteMmio32(gRuntimeBase + 0x24, 0);
    WriteMmio32(gRuntimeBase + 0x28, 1);
    WriteMmio32(gRuntimeBase + 0x2C, 0);
    WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
    if (RealPc) {
        WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing));
    } else {
        WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));
    }

    Fence();
    Sts = ReadMmio32(gOperationalBase + 4);
    BootLog("boot: xhci USBSTS before RS=");
    if (!RealPc) {
        HalSerialFormatHex(B, Sts, 8);
        HalSerialWrite(B);
        HalSerialWrite("\n");
    } else {
        HalSerialFormatHex(B, Sts, 8);
        {
            char Msg[48];
            int n = 0;
            const char *P = "boot: xhci STS=";
            while (*P && n < 20) {
                Msg[n++] = *P++;
            }
            Msg[n++] = B[0]; Msg[n++] = B[1]; Msg[n++] = B[2]; Msg[n++] = B[3];
            Msg[n++] = B[4]; Msg[n++] = B[5]; Msg[n++] = B[6]; Msg[n++] = B[7];
            Msg[n++] = '\n';
            Msg[n] = 0;
            HalSerialBootMark(Msg);
        }
    }

    /*
     * 分步：若卡在 before→after 之间=写 RS 挂；卡在 after 后=读 USBSTS/DMA 挂。
     * 真机勿 cli（SMI/固件可能需要）。
     */
    HalSerialBootMark("boot: xhci before RS\n");
    if (HalCpuIsHypervisor()) {
        WriteMmio32(gOperationalBase, USBCMD_RS | USBCMD_INTE);
    } else {
        WriteMmio32(gOperationalBase, USBCMD_RS);
    }
    Fence();
    HalSerialBootMark("boot: xhci after RS\n");

    if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, RealPc ? 100000 : 1000000)) {
        BootLog("boot: xhci run timeout\n");
        return 0;
    }
    Sts = ReadMmio32(gOperationalBase + 4);
    HalSerialBootMark("boot: xhci RS running\n");
    (void)Sts;
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

/* 只给已连接口上电，避免 18 口全写 */
static void PowerConnectedPorts(void) {
    UINT32 p;
    volatile int D;

    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (!(Val & PORTSC_CCS)) {
            continue;
        }
        if (!(Val & PORTSC_PP)) {
            WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        }
    }
    for (D = 0; D < 50000; D++) {
    }
}

/*
 * 使能已连接端口。
 * 真机：在 cli + GOP mute 下允许短超时 PR（禁写 change 清除位）；
 * 缓存映射须 UC，否则 PORTSC 写易挂。
 */
static int ResetPort(UINT32 Port1) {
    UINT64 Ps = gOperationalBase + PortReg(Port1);
    UINT32 Val = ReadMmio32(Ps);
    UINT32 Speed;
    int t;
    char B[12];
    int RealPc = !HalCpuIsHypervisor();

    if (!(Val & PORTSC_CCS)) {
        return 0;
    }

    HalSerialWrite("boot: xhci portsc=");
    HalSerialFormatHex(B, Val, 8);
    HalSerialWrite(B);
    HalSerialWrite("\n");
    if (RealPc) {
        char Msg[48];
        int n = 0;
        const char *P = "boot: xhci portsc=";
        while (*P && n < 24) {
            Msg[n++] = *P++;
        }
        Msg[n++] = B[0]; Msg[n++] = B[1]; Msg[n++] = B[2]; Msg[n++] = B[3];
        Msg[n++] = B[4]; Msg[n++] = B[5]; Msg[n++] = B[6]; Msg[n++] = B[7];
        Msg[n++] = '\n';
        Msg[n] = 0;
        BootLog(Msg);
    }

    if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        BootLog("boot: xhci port already PED\n");
        return 1;
    }

    if (!(Val & PORTSC_PP)) {
        WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        if (!WaitSet(Ps, PORTSC_PP, RealPc ? 20000 : 30000)) {
            BootLog("boot: xhci PP timeout\n");
            return 0;
        }
        Val = ReadMmio32(Ps);
    }

    Speed = PortSpeed(Val);
    BootLog("boot: xhci port reset...\n");
    Val = PortscNeutral(ReadMmio32(Ps));
    if (Speed >= 4) {
        WriteMmio32(Ps, Val | PORTSC_WPR | PORTSC_PP);
    } else {
        WriteMmio32(Ps, Val | PORTSC_PR | PORTSC_PP);
    }

    if (!WaitSet(Ps, PORTSC_PRC | PORTSC_WRC, RealPc ? 40000 : 60000)) {
        BootLog("boot: xhci reset timeout\n");
        return 0;
    }

    /* 真机：勿再写 PORTSC 清 change（易二次挂）；只轮询 PED */
    if (!RealPc) {
        Val = ReadMmio32(Ps);
        WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PRC | PORTSC_WRC | PORTSC_CSC);
    }
    for (t = 0; t < (RealPc ? 20000 : 30000); t++) {
        Val = ReadMmio32(Ps);
        if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
            BootLog("boot: xhci port enabled\n");
            return 1;
        }
    }
    BootLog("boot: xhci port not PED\n");
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

static int AddressDeviceOnPort(UINT32 Port1, UINT8 Speed, UINT32 *SlotOut,
                               UINT8 *DevCtx) {
    gXferSlot = 0;
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), SlotOut) < 0 || *SlotOut == 0 ||
        *SlotOut > DCBAA_SLOTS) {
        DebugWrite("XHCI: Enable Slot failed\n");
        return 0;
    }
    DebugWrite("XHCI: Slot ");
    DebugHex32(*SlotOut);
    DebugWrite("\n");

    gXferSlot = *SlotOut;
    gDcbaa[*SlotOut] = PointerToPhysical(DevCtx);
    ZeroMemory(DevCtx, 2048);
    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (1u << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)Port1 << 16;

    InitRing(gEp0Ring, &gEp0);
    UINT32 *Ep0 = (UINT32 *)(void *)InEp(1);
    gEp0Mps = SpeedMps(Speed);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)gEp0Mps << 16);
    UINT64 Deq = PointerToPhysical(gEp0Ring) | 1;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(*SlotOut), 0) < 0) {
        DebugWrite("XHCI: Address Device failed\n");
        return 0;
    }
    DebugWrite("XHCI: Address Device OK\n");
    return 1;
}

/* Enable Slot + Address Device 命令序列 */
static int AddressDevice(UINT32 Port1, UINT8 Speed) {
    gXferSlot = gSlotId;
    return AddressDeviceOnPort(Port1, Speed, &gSlotId, gDevCtx);
}

static void DisableSlot(UINT32 SlotId) {
    if (SlotId == 0 || SlotId > DCBAA_SLOTS) {
        return;
    }
    (void)Command(0, TRB_TYPE(TRB_DISABLE_SLOT) | TRB_SLOT(SlotId), 0);
    gDcbaa[SlotId] = 0;
    if (gSlotId == SlotId) {
        gSlotId = 0;
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
        DebugWrite("XHCI: EP0 transfer failed cc=");
        DebugHex32(gXferCode);
        DebugWrite("\n");
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
    ZeroMemory(Buf, Length);
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

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << gIntrDci);

    UINT32 *Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = ((UINT32)gIntrDci << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)gPort1 << 16;

    InitRing(gIntrRing, &gIntr);
    UINT32 *Ep = (UINT32 *)(void *)InEp(gIntrDci);
    UINT8 Interval = (Speed >= 3) ? (UINT8)((BInterval > 0) ? (BInterval - 1) : 0) : FsInterval(BInterval);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)Mps << 16);
    UINT64 Deq = PointerToPhysical(gIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(gSlotId), 0) < 0) {
        DebugWrite("XHCI: Configure Endpoint failed\n");
        return 0;
    }
    DebugWrite("XHCI: Interrupt EP configured\n");
    return 1;
}

/* 提交中断 IN 传输 TRB */
static void QueueIntr(void) {
    gIntrDone = 0;
    Enqueue(gIntrRing, &gIntr, PointerToPhysical(gReportBuf), 8, TRB_TYPE(TRB_NORMAL) | TRB_IOC);
    RingDoorbell(gSlotId, gIntrDci);
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
            /* Boot keyboard 3/1/1；部分键鼠固件报 Proto=0 仍可用 boot report */
            if (Class == 3 && Sub == 1 && (Proto == 1 || Proto == 0)) {
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

    ZeroMemory(gInCtx, sizeof(gInCtx));
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
    UINT64 Deq = PointerToPhysical(gMouseIntrRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = Mps;

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        return 0;
    }
    return 1;
}

static void QueueMouseIntr(void) {
    gMouseIntrDone = 0;
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
    UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(Port1));
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    if (!ResetPort(Port1)) {
        return 0;
    }
    UINT8 Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
    gMousePort = Port1;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx)) {
        gMouseSlotId = 0;
        return 0;
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 0)) {
        DebugWrite("XHCI: mouse config failed\n");
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
        DebugWrite("XHCI: mouse no interrupt EP\n");
        gMouseSlotId = 0;
        return 0;
    }
    if (!ConfigureMouseIntr(gMouseSlotId, EpAddr, Mps, Interval, Speed)) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    QueueMouseIntr();
    DebugWrite("XHCI: mouse ready\n");
    return 1;
}

/* 完整 xHCI 初始化：复位、建环、枚举端口上的 USB 键盘 */
int XhciInit(UINT64 BaseAddress) {
    int RealPc = !HalCpuIsHypervisor();
    char B[12];

    /*
     * 实测：白字最后停在 ports=0x12 且无 B10 黄字 → Present 在该行可能不返回。
     * 真机：一进 Init 就 mute，ports/探针全走 BootMark（直写帧缓冲）。
     */
    if (RealPc) {
        HalSerialGopMute(1);
        HalSerialBootMark("boot: xhci-B10 enter\n");
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
     * 真机 B10 探针（只测一件事）：黄字 before RS / after RS / RS running。
     * 本核不做端口枚举；测完即停。
     */
    if (RealPc) {
        HalSerialBootMark("boot: xhci-B10 probe\n");
        HalSerialBootMark("boot: xhci-B10 HCRST\n");
        if (!ResetController()) {
            HalSerialGopMute(0);
            HalSerialWrite("boot: xhci-B10 HCRST fail\n");
            return 1;
        }
        HalSerialBootMark("boot: xhci-B10 Start\n");
        if (!StartController(MaxSlots)) {
            HalSerialGopMute(0);
            HalSerialWrite("boot: xhci-B10 RS fail/timeout\n");
            return 1;
        }
        HalSerialGopMute(0);
        HalSerialWrite("boot: xhci-B10 RS ok, probe stop\n");
        return 1;
    }

    BootLog("boot: xhci take legacy...\n");
    TakeLegacy();
    BootLog("boot: xhci after legacy\n");

    if (!ResetController() || !StartController(MaxSlots)) {
        return 0;
    }
    HalSerialWrite("boot: xhci controller running\n");
    DebugWrite("XHCI: controller running\n");
    HalSerialWrite("boot: xhci poll HID (IOAPIC not required)\n");
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
        }
    }

    UINT32 Port1 = 0;
    UINT8 Speed = 0;
    UINT8 EpAddr = 0, Interval = 10;
    UINT16 Mps = 8;
    UINT8 ConfigVal = 1;
    int HaveIntr = 0;

    /* 以下仅 QEMU（真机已在上方 return） */
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

                HalSerialWrite("boot: xhci address...\n");
                if (!AddressDevice(p, Speed)) {
                    HalSerialWrite("boot: xhci address fail\n");
                    DisableSlot(gSlotId);
                    continue;
                }
                HalSerialWrite("boot: xhci address ok\n");

                if (GetDesc(0x0100, 0, 18, gCtrlBuf) < 0) {
                    DisableSlot(gSlotId);
                    continue;
                }
                if (GetDesc(0x0200, 0, 9, gCtrlBuf) < 0) {
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
                    DisableSlot(gSlotId);
                    continue;
                }
                if (SetConfig(ConfigVal) < 0) {
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
                for (volatile int d = 0; d < 40000; d++) {
                }
            }
        }
    }

    if (Port1 == 0) {
        HalSerialWrite("boot: xhci up but no HID keyboard\n");
        DebugWrite("XHCI: no keyboard\n");
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    HalSerialWrite("boot: xhci-hid poll ready\n");

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

    if (Next == gMouseReadIndex) {
        return;
    }
    USB_MOUSE_REPORT *R = &gMouseQ[gMouseWriteIndex];
    R->Wheel = 0;
    X0 = (UINT32)(gMouseBuf[1] | (gMouseBuf[2] << 8));
    Y0 = (UINT32)(gMouseBuf[3] | (gMouseBuf[4] << 8));
    X1 = (UINT32)(gMouseBuf[2] | (gMouseBuf[3] << 8));
    Y1 = (UINT32)(gMouseBuf[4] | (gMouseBuf[5] << 8));

    if (gMouseReportLen >= 6 && X0 <= 32767 && Y0 <= 32767) {
        /* 无 Report ID 的绝对/扩展：buttons + 16-bit X/Y；滚轮常在 byte5 */
        R->Buttons = gMouseBuf[0] & 7;
        R->X = X0;
        R->Y = Y0;
        if (gMouseReportLen >= 6) {
            R->Wheel = (INT8)gMouseBuf[5];
        }
    } else if (gMouseReportLen >= 7 && X1 <= 32767 && Y1 <= 32767) {
        /* 带 Report ID：id + buttons + 16-bit X/Y；滚轮常在 byte6 */
        R->Buttons = gMouseBuf[1] & 7;
        R->X = X1;
        R->Y = Y1;
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

/* 排空事件环：轮询不依赖 IMAN；IRQ 模式仍走 XhciIrq */
void XhciDrainEvents(void) {
    int i;

    if (gUseIrq) {
        for (i = 0; i < 8; i++) {
            if (!(ReadMmio32(gRuntimeBase + 0x20) & 1u)) {
                break;
            }
            XhciIrq();
        }
        return;
    }

    for (i = 0; i < 32; i++) {
        ProcessEvents();
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
}

/* 通过 PciEnableMsi 绑定中断向量；失败仍可 Poll 排空事件环（PR-H2） */
int XhciEnableIrq(USB_CONTROLLER *Device) {
    if (gUseGetReport || gSlotId == 0) {
        DebugWrite("XHCI: no interrupt EP, IRQ unused\n");
        gUseIrq = 0;
        return 0;
    }
    if (!PciEnableMsi(Device, VEC_XHCI)) {
        DebugWrite("XHCI: MSI failed; poll drain\n");
        gUseIrq = 0;
        XhciDrainEvents();
        return 0;
    }
    gUseIrq = 1;
    XhciDrainEvents();
    return 1;
}

/* 返回是否使用 MSI-X 中断模式（否则为 GET_REPORT 轮询） */
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
    if (gKeyboardReadIndex == gKeyboardWriteIndex) {
        return 0;
    }
    UINT8 *Src = (UINT8 *)&gKbdQ[gKeyboardReadIndex];
    UINT8 *Dst = (UINT8 *)Report;
    for (int i = 0; i < 8; i++) {
        Dst[i] = Src[i];
    }
    gKeyboardReadIndex = (gKeyboardReadIndex + 1) % KBD_Q;
    return 1;
}

int XhciMousePresent(void) {
    return gMouseSlotId != 0;
}

int XhciDequeueMouse(USB_MOUSE_REPORT *Report) {
    if (gMouseReadIndex == gMouseWriteIndex) {
        return 0;
    }
    *Report = gMouseQ[gMouseReadIndex];
    gMouseReadIndex = (gMouseReadIndex + 1) % MOUSE_Q;
    return 1;
}
