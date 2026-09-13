/*
 * XhciCore.c — Init/Ready/Abandon 与共享全局（Keyboard/Device/Controller/Transfer/Command/Ring/Mmio 已拆）
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

