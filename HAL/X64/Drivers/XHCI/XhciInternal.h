/*
 * XhciInternal.h — PR-H-xhci-split-1：xHCI 内部共享（宏/类型/extern/共享声明）
 *
 * 对外仍用 Drivers/XHCI.h。本头供 Drivers/XHCI.c 与后续拆分出的 .c 共用。
 * split-2 起：各 .c 直接 #include；全局非 static 定义在归属文件，此处 extern。
 * 共享函数声明供跨文件调用；尚未搬走的实现仍在 XHCI.c（非 static）。
 */
#ifndef XHCI_INTERNAL_H
#define XHCI_INTERNAL_H

#include "XHCI.h"
#include "Hal.h"
#include "Debug.h"
#include "ToySerialLog.h"
#include "AcpiMadt.h"
#include "Platform.h"
#include "SpinLock.h"
#include "VirtualMemory.h"

/* ---- 寄存器 / 环 / 诊断宏（从单体 XHCI.c 原样抽出） ---- */
#ifndef PTE_PWT
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#endif
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
#define KBD_Q 16
#ifndef XHCI_DIAG_VERBOSE
#define XHCI_DIAG_VERBOSE 0
#endif
#define MOUSE_Q 32
#define XHCI_SCRATCH_MAX 128
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

/* ---- 全局（定义仍在 XHCI.c；拆文件后按 XHCI拆分.md §五 分置） ---- */
extern UINT64 gCapabilityBase;
extern UINT64 gOperationalBase;
extern UINT64 gDoorbellBase;
extern UINT64 gRuntimeBase;
extern UINT32 gCtxSize;
extern UINT32 gMaxPorts;
extern int gXhciStarted;

extern int gXhciDmar;
extern int gXhciTe;

extern UINT32 gPort1;
extern UINT8 gSpeed;
extern UINT32 gSlotId;
extern UINT32 gXferSlot;
extern UINT32 gIntrDci;
extern UINT16 gEp0Mps;
extern UINT8 gKbdIface;
extern UINT8 gKbdParseScore;
extern UINT8 gKbdEpAddr;
extern UINT16 gKbdMps;
extern UINT8 gKbdEpInterval;
extern UINT8 gUseGetReport;
extern UINT8 gKbdPollReport;
extern UINT8 gKbdReportPrev[8];
extern volatile UINT32 gGetReportBusy;
extern UINT8 gGetReportFails;
extern UINT8 gXferFast;
extern UINT8 gUseIrq;
extern XHCI_IRQ_MODE gIrqMode;
extern UINT32 gKbdRoute;
extern UINT8 gKbdHubSlot;
extern UINT8 gKbdTtPort;
extern const char *gEnumWhy;

extern USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
extern volatile UINT32 gKeyboardWriteIndex;
extern volatile UINT32 gKeyboardReadIndex;

extern UINT32 gMouseSlotId;
extern UINT32 gMousePort;
extern UINT32 gMouseRoute;
extern UINT8 gMouseHubSlot;
extern UINT8 gMouseTtPort;
extern UINT32 gMouseIntrDci;
extern UINT8 gMouseIface;
extern UINT8 gMouseIfaceProto;
extern UINT8 gMouseParseScore;
extern UINT8 gMouseAbsolute;
extern UINT8 gMouseEpAddr;
extern UINT8 gMouseReportLen;
extern UINT8 gMouseXferLen;
extern UINT8 gMouseBuf[8];
extern int gMouseAbsX;
extern int gMouseAbsY;
extern int gMouseAbsInit;
extern XHCI_TRB gMouseIntrRing[RING_SIZE];
extern RING_STATE gMouseIntr;
extern UINT8 gMouseDevCtx[2048];
extern volatile UINT32 gMouseIntrDone;
extern volatile UINT32 gIntrReportReady;
extern volatile UINT32 gMouseReportReady;

extern XHCI_TRB gBulkInRing[RING_SIZE];
extern XHCI_TRB gBulkOutRing[RING_SIZE];
extern RING_STATE gBulkIn;
extern RING_STATE gBulkOut;
extern int gMscBulkRingsInited;

/* PR-H-msc-3：scan 临时 slot（文档 §3.4 补全） */
extern UINT32 gMscScanSlot;
extern UINT8 gMscScanDevCtx[2048];
extern XHCI_TRB gMscScanEp0Ring[RING_SIZE];
extern RING_STATE gMscScanEp0;

extern volatile UINT32 gStatIntrEvt;
extern volatile UINT32 gStatMouseEvt;
extern volatile UINT32 gStatKbdPush;
extern volatile UINT32 gStatMousePush;
extern volatile UINT32 gStatLastCc;
extern volatile UINT32 gStatDrain;
extern volatile UINT32 gStatXferAny;
extern volatile UINT32 gStatEvtRing;
extern volatile UINT32 gStatLastSlot;
extern volatile UINT32 gStatLastEp;
extern volatile UINT32 gStatUnmatched;
extern volatile UINT32 gStatIrq;
extern UINT32 gDiagXferLogged;
extern UINT32 gDiagQuiet;
extern UINT32 gDiagIntrCcLogged;
extern UINT32 gCtrlFailLogged;

extern USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
extern volatile UINT32 gMouseWriteIndex;
extern volatile UINT32 gMouseReadIndex;
extern SPIN_LOCK gHidQueueLock;

extern XHCI_TRB gCmdRing[RING_SIZE];
extern XHCI_TRB gEp0Ring[RING_SIZE];
extern XHCI_TRB gHubEp0Ring[RING_SIZE];
extern XHCI_TRB gMouseEp0Ring[RING_SIZE];
extern XHCI_TRB gIntrRing[RING_SIZE];
extern XHCI_TRB gEvtRing[EVT_SIZE];
extern XHCI_TRB *gCmdRingLive;
extern XHCI_TRB *gEvtRingLive;
extern UINT32 gEvtRingSize;
extern RING_STATE gCmd;
extern RING_STATE gEp0;
extern RING_STATE gHubEp0;
extern RING_STATE gMouseEp0;
extern RING_STATE gIntr;
extern UINT32 gEvtDeq;
extern UINT32 gEvtCcs;

extern UINT64 gDcbaa[DCBAA_SLOTS + 1];
extern UINT64 *gDcbaaLive;
extern UINT32 gDcbaaMaxSlot;
extern int gDcbaaFromFirmware;
extern UINT64 gFwDcbaapSave;
extern UINT64 gFwCrcrSave;
extern UINT32 gFwCrcrRcs;
extern UINT64 gFwErstbaSave;
extern UINT64 gFwEvtSave;
extern UINT16 gFwEvtSegSave;
extern UINT64 gFwErdpSave;

extern UINT64 gScratchPtr[XHCI_SCRATCH_MAX];
extern UINT8 gScratchBuf[XHCI_SCRATCH_MAX][4096];

extern UINT8 gDevCtx[2048];
extern UINT8 gHubDevCtx[2048];
extern UINT8 gInCtx[2048];
extern UINT8 gCtrlBuf[256];
extern UINT8 gReportBuf[8];
extern UINT8 gErst[16];

extern UINT32 gHubSlotId;
extern UINT32 gHubRootPort;
extern UINT8 gHubNumPorts;
extern UINT8 gHubSpeed;
extern UINT8 gHubMtt;
extern UINT8 gHubTtt;
extern UINT32 gPortNoHid;
extern UINT32 gPortNeedForcePr;
extern UINT8 gSlotEp0UsesKbdRing[DCBAA_SLOTS + 1];

extern volatile UINT32 gCmdDone;
extern UINT32 gCmdCode;
extern UINT32 gCmdSlot;
extern volatile UINT32 gXferDone;
extern UINT32 gXferCode;
extern UINT32 gXferRemain;
extern volatile UINT32 gIntrDone;

/* ---- 共享函数（实现仍在 XHCI.c；后续按模块搬走） ---- */
UINT32 ReadMmio32(UINT64 Addr);
void WriteMmio32(UINT64 Addr, UINT32 Value);
void WriteMmio64(UINT64 Addr, UINT64 Value);
UINT64 ReadMmio64(UINT64 Addr);
void FlushDma(const void *Ptr, UINTN Size);
void ZeroMemory(void *Ptr, UINTN Size);
void CopyMemory(void *Dst, const void *Src, UINTN Size);
UINT64 PointerToPhysical(const void *Ptr);
void Fence(void);
UINT64 ReadTsc(void);
void StallMs(UINT32 Ms);
int WaitClear(UINT64 Addr, UINT32 Mask, int Timeout);
int WaitSet(UINT64 Addr, UINT32 Mask, int Timeout);
int WaitSetMs(UINT64 Addr, UINT32 Mask, UINT32 Ms);
int WaitClearMs(UINT64 Addr, UINT32 Mask, UINT32 Ms);

void InitRing(XHCI_TRB *Ring, RING_STATE *St, UINT32 Size);
void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control);
UINT32 TrbType(UINT32 Control);
void ProcessEvents(void);
void ProcessEventsRealPc(void);
void RingDoorbell(UINT32 Slot, UINT32 Target);

void DcbaaSet(UINT32 Slot, UINT64 Phys);
void DcbaaFlush(void);

UINT8 *InSlot(void);
UINT8 *InEp(UINT32 Dci);

int ResetController(void);
int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut);
void RecoverCommandRing(void);
int WaitCommand(int Timeout);
int WaitTransfer(int Timeout);
void ServiceHidCompletions(void);

UINT32 PortReg(UINT32 Port1);
UINT8 PortSpeed(UINT32 Portsc);
UINT32 PortscNeutral(UINT32 State);
void PortscClearChange(UINT64 Ps);
void PowerConnectedPorts(void);
int ResetPortEx(UINT32 Port1, int Force);
int ResetPort(UINT32 Port1);

UINT16 SpeedMps(UINT8 Speed);
void Ep0RingForSlot(UINT32 SlotId, XHCI_TRB **RingOut, RING_STATE **StOut);
void Ep0RingForSlotOut(UINT32 *SlotOut, XHCI_TRB **RingOut, RING_STATE **StOut);
int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                        UINT8 *DevCtx, UINT32 RouteString,
                        UINT8 ParentHubSlot, UINT8 TtPort,
                        int HubDevice, UINT8 HubNumPorts);
int AddressDevice(UINT32 Port1, UINT8 Speed);
int ControlXfer(USB_SETUP_PACKET *Setup, void *Data);
int GetDesc(UINT16 TypeIndex, UINT16 Index, UINT16 Length, void *Buf);
int EvaluateHubSlot(UINT32 SlotId, UINT32 RootPort, UINT8 Speed, UINT8 NumPorts);
void HubNoteMttFromDevDesc(UINT8 Speed);
int EvaluateEp0(UINT32 SlotId, UINT16 Mps);
int GetDeviceDesc(void);
int SetConfig(UINT8 Config);
int SetInterface(UINT8 Iface, UINT8 Alt);
int SetIdle(UINT8 Iface);
int SetProtocolBoot(UINT8 Iface);
int SetReportOutput(UINT8 Iface, void *Data, UINT16 Length);
void DisableSlot(UINT32 SlotId);
void RecoverEp0(UINT32 SlotId);

int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                     UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed,
                  UINT8 MouseEpAddr, UINT16 MouseMps, UINT8 MouseBInterval);
int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval,
                       UINT8 Speed);
void QueueIntr(void);
void QueueMouseIntr(void);
UINT8 FsInterval(UINT8 BInterval);
int HidGetInputReport(UINT8 Iface, void *Data, UINT16 Length);
int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St, UINTN RingBytes);
int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KbdIface, UINT8 KbdEp,
                       UINT8 *MouseEp, UINT16 *MouseMps, UINT8 *MouseIv);
int RealPcRejectMouseExtraAsKeyboard(UINT16 Total, UINT8 Speed);
int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                              UINT8 ConfigVal);

int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                   int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                  UINT16 *, UINT8 *),
                   int UseBootProto);
int IsHubDeviceDesc(void);
int ConfigHasHubIface(UINT8 *Cfg, UINT16 Total);
int TryConfigureKeyboardSlot(UINT8 Speed);
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot);
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed);
int EnumHubChildrenForKeyboard(void);
int EnumHubChildrenForMouse(void);

int InitMouseOnPort(UINT32 Port1);
int InitMouseOnKeyboardSlot(void);
void MousePush(void);
void KbdPush(void);
/* XhciInitMouseDeferred / XhciMouse* 对外见 XHCI.h */

void EnableHostInterrupts(void);
void ImClearPending(void);

void BootLog(const char *Text);
void BootLogHex(const char *Prefix, UINT64 Value, int Digits);
void BootLogV(const char *Text);
void BootLogHexV(const char *Prefix, UINT64 Value, int Digits);
void BootMarkV(const char *Text);
void EnumWhy(const char *Why);
int DiagVerbose(void);
void DiagChk(const char *Step, int Ok, const char *Want, UINT64 Got, int Digits);
void DiagChkStr(const char *Step, int Ok, const char *Want, const char *Got);
const char *CmdTrbName(UINT32 Control);
void XhciPollKbdGetReport(void);


#endif /* XHCI_INTERNAL_H */

