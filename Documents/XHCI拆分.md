# 任务：把 HAL/X64/Drivers/XHCI.c 拆分为 8 个文件 + 1 个内部头文件

## 一、背景

XHCI.c 约 2900 行，包含 MMIO 工具、TRB 环管理、控制器生命周期、
端口复位、设备枚举、HID 解析、Hub 支持、鼠标处理、中断处理、诊断日志。
需要按功能拆分成多个文件，让贡献者能独立修改某一模块。

## 二、目录结构

创建目录 HAL/X64/Drivers/XHCI/，最终结构：

```
HAL/X64/Drivers/XHCI/
├── XhciInternal.h    # 内部共享头文件（宏、结构体、全局变量 extern、共享函数声明）
├── XhciCore.c        # 核心：MMIO、TRB 环、控制器生命周期、命令提交
├── XhciPort.c        # 端口：复位、上电、状态管理
├── XhciDevice.c      # 设备：枚举、控制传输、描述符
├── XhciHid.c         # HID：配置解析、中断端点、HID 请求
├── XhciHub.c         # Hub：根口 hub、子设备枚举
├── XhciMouse.c       # 鼠标：独立口、复合、报告解析
├── XhciIrq.c         # 中断：ISR、Drain、Fallback
└── XhciDiag.c        # 诊断：计数器、日志、格式化
```

**注意**：HAL/X64/Drivers/XHCI.h 保持不动（对外接口不变）。

## 三、XhciInternal.h 的内容

必须包含以下内容（从 XHCI.c 中提取）：

### 3.1 头文件保护与 include

```c
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

#endif
```

> **split-1 落地约定**：`XHCI.c` 在 `#include "XHCI/XhciInternal.h"` 前定义 `XHCI_INTERNAL_IMPLEMENTATION`，只吃宏/类型；`extern` 与共享函数原型包在 `#ifndef XHCI_INTERNAL_IMPLEMENTATION` 内，避免与本文件仍为 `static` 的定义冲突。后续拆出的 `.c` 不定义该宏。
### 3.2 所有寄存器宏定义

从 XHCI.c 提取以下宏定义（一个不漏）：

- PTE_PWT / PTE_PCD / PTE_XHCI_DMA
- RING_SIZE / EVT_SIZE / DCBAA_SLOTS / XHCI_FW_CMD_SIZE / XHCI_FW_EVT_MAX
- PORTSC_* (CCS, PED, OCA, PR, PP, SPEED_SHIFT, CSC, PEC, WRC, OCC, PRC, PLC, CEC, WPR, RO, RWS, CHANGE)
- USBCMD_* (RS, HCRST, INTE)
- USBSTS_* (HCH, HSE, EINT, CNR)
- CRCR_* (CA, CRR)
- TRB_* (C, TC, ISP, IOC, IDT, TYPE, SLOT, TRT_OUT, TRT_IN, DIR_IN)
- TRB 类型常量 (TRB_NORMAL, TRB_SETUP, ..., TRB_RESET_EP, TRB_STOP_EP, TRB_SET_TR_DEQ, TRB_TRANSFER_EVENT, TRB_CMD_COMPLETION)
- CC_* (SUCCESS, SHORT_PACKET, CONTEXT_STATE, STOPPED, STOPPED_LEN, STOPPED_SHORT)
- HUB_* (PORT_CONNECTION, PORT_ENABLE, PORT_RESET, PORT_POWER, C_PORT_CONNECTION, C_PORT_RESET, FEAT_PORT_RESET, FEAT_PORT_POWER, FEAT_C_PORT_CONNECTION, FEAT_C_PORT_RESET)
- XHCI_SCRATCH_MAX / KBD_Q / MOUSE_Q
- XHCI_DIAG_VERBOSE (默认 0)

### 3.3 所有类型定义

```c
typedef struct {
    UINT64 Parameter;
    UINT32 Status;
    UINT32 Control;
} __attribute__((packed, aligned(16))) XHCI_TRB;

typedef struct {
    UINT32 Enq;
    UINT32 Pcs;
    UINT32 Size;
} RING_STATE;
```

### 3.4 所有全局变量（extern 声明）

从 XHCI.c 提取以下全局变量，改为 extern 声明：

```c
// 基址
extern UINT64 gCapabilityBase;
extern UINT64 gOperationalBase;
extern UINT64 gDoorbellBase;
extern UINT64 gRuntimeBase;
extern UINT32 gCtxSize;
extern UINT32 gMaxPorts;
extern int gXhciStarted;

// 探针
extern int gXhciDmar;
extern int gXhciTe;

// 键盘状态
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

// 键盘队列
extern USB_KEYBOARD_REPORT gKbdQ[KBD_Q];
extern volatile UINT32 gKeyboardWriteIndex;
extern volatile UINT32 gKeyboardReadIndex;

// 鼠标状态
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

// MSC Bulk（占位）
extern XHCI_TRB gBulkInRing[RING_SIZE];
extern XHCI_TRB gBulkOutRing[RING_SIZE];
extern RING_STATE gBulkIn;
extern RING_STATE gBulkOut;
extern int gMscBulkRingsInited;

// MSC scan 临时 slot（PR-H-msc-3；与 Bulk 壳并列）
extern UINT32 gMscScanSlot;
extern UINT8 gMscScanDevCtx[2048];
extern XHCI_TRB gMscScanEp0Ring[RING_SIZE];
extern RING_STATE gMscScanEp0;

// 统计
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

// 鼠标队列
extern USB_MOUSE_REPORT gMouseQ[MOUSE_Q];
extern volatile UINT32 gMouseWriteIndex;
extern volatile UINT32 gMouseReadIndex;
extern SPIN_LOCK gHidQueueLock;

// TRB 环
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

// DCBAA
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

// Scratchpad
extern UINT64 gScratchPtr[XHCI_SCRATCH_MAX];
extern UINT8 gScratchBuf[XHCI_SCRATCH_MAX][4096];

// 上下文缓冲
extern UINT8 gDevCtx[2048];
extern UINT8 gHubDevCtx[2048];
extern UINT8 gInCtx[2048];
extern UINT8 gCtrlBuf[256];
extern UINT8 gReportBuf[8];
extern UINT8 gErst[16];

// Hub 状态
extern UINT32 gHubSlotId;
extern UINT32 gHubRootPort;
extern UINT8 gHubNumPorts;
extern UINT8 gHubSpeed;
extern UINT8 gHubMtt;
extern UINT8 gHubTtt;
extern UINT32 gPortNoHid;
extern UINT32 gPortNeedForcePr;
extern UINT8 gSlotEp0UsesKbdRing[DCBAA_SLOTS + 1];

// 命令/传输完成标志
extern volatile UINT32 gCmdDone;
extern UINT32 gCmdCode;
extern UINT32 gCmdSlot;
extern volatile UINT32 gXferDone;
extern UINT32 gXferCode;
extern UINT32 gXferRemain;
extern volatile UINT32 gIntrDone;
```

### 3.5 共享函数声明

```c
// MMIO 工具（XhciCore.c 实现）
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

// TRB 环（XhciCore.c 实现）
void InitRing(XHCI_TRB *Ring, RING_STATE *St, UINT32 Size);
void Enqueue(XHCI_TRB *Ring, RING_STATE *St, UINT64 Param, UINT32 Status, UINT32 Control);
UINT32 TrbType(UINT32 Control);
void ProcessEvents(void);
void ProcessEventsRealPc(void);
void RingDoorbell(UINT32 Slot, UINT32 Target);

// DCBAA（XhciCore.c 实现）
void DcbaaSet(UINT32 Slot, UINT64 Phys);
void DcbaaFlush(void);

// 控制器生命周期（XhciCore.c 实现）
int ResetController(void);
int Command(UINT64 Param, UINT32 Control, UINT32 *SlotOut);
void RecoverCommandRing(void);
int WaitCommand(int Timeout);
int WaitTransfer(int Timeout);
void ServiceHidCompletions(void);

// 端口（XhciPort.c 实现）
UINT32 PortReg(UINT32 Port1);
UINT8 PortSpeed(UINT32 Portsc);
UINT32 PortscNeutral(UINT32 State);
void PowerConnectedPorts(void);
int ResetPortEx(UINT32 Port1, int Force);
int ResetPort(UINT32 Port1);

// 设备（XhciDevice.c 实现）
int AddressDeviceOnPort(UINT32 RootPort, UINT8 Speed, UINT32 *SlotOut,
                        UINT8 *DevCtx, UINT32 RouteString,
                        UINT8 ParentHubSlot, UINT8 TtPort,
                        int HubDevice, UINT8 HubNumPorts);
int GetDeviceDesc(void);
int SetConfig(UINT8 Config);
int SetInterface(UINT8 Iface, UINT8 Alt);
int SetIdle(UINT8 Iface);
int SetProtocolBoot(UINT8 Iface);
int SetReportOutput(UINT8 Iface, void *Data, UINT16 Length);
void DisableSlot(UINT32 SlotId);
void RecoverEp0(UINT32 SlotId);

// HID（XhciHid.c 实现）
int ParseConfig(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int ParseConfigMouse(UINT8 *Cfg, UINT16 Total, UINT8 Speed,
                     UINT8 *Iface, UINT8 *EpAddr, UINT16 *Mps, UINT8 *Interval);
int ConfigureIntr(UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed,
                  UINT8 MouseEpAddr, UINT16 MouseMps, UINT8 MouseBInterval);
int ConfigureMouseIntr(UINT32 SlotId, UINT8 EpAddr, UINT16 Mps, UINT8 BInterval, UINT8 Speed);
void QueueIntr(void);
void QueueMouseIntr(void);
UINT8 FsInterval(UINT8 BInterval);
int HidGetInputReport(UINT8 Iface, void *Data, UINT16 Length);
int SyncIntrDequeue(UINT32 Slot, UINT32 Dci, XHCI_TRB *Ring, RING_STATE *St, UINTN RingBytes);

// Hub（XhciHub.c 实现）
int ClaimHubOnRootPort(UINT32 RootPort, UINT8 Speed, UINT32 ExistingSlot);
int TryHubOnRootPort(UINT32 RootPort, UINT8 Speed);
int EnumHubChildrenForKeyboard(void);
int EnumHubChildrenForMouse(void);

// 鼠标（XhciMouse.c 实现）
int InitMouseOnPort(UINT32 Port1);
int InitMouseOnKeyboardSlot(void);
void MousePush(void);

// 键盘（XhciDevice.c 或独立文件）
void KbdPush(void);

// 中断（XhciIrq.c 实现）
void EnableHostInterrupts(void);
void ImClearPending(void);

// 诊断（XhciDiag.c 实现）
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
```

## 四、每个 .c 文件的实现内容

### 4.1 XhciCore.c

包含以下函数（从 XHCI.c 原样搬过来，不改逻辑）：

- `ReadMmio32` / `WriteMmio32` / `WriteMmio64` / `ReadMmio64`
- `FlushDma` / `ZeroMemory` / `CopyMemory` / `PointerToPhysical` / `Fence`
- `ReadTsc` / `StallMs` / `WaitClear` / `WaitSet` / `WaitSetMs` / `WaitClearMs`
- `InitRing` / `Enqueue` / `TrbType` / `ProcessEvents` / `ProcessEventsRealPc`
- `RingDoorbell`
- `DcbaaSet` / `DcbaaFlush`
- `MapXhciDma` / `ResolveFwCmdRing`
- `RecoverCommandRing` / `Command`
- `WaitCommand` / `WaitTransfer` / `ServiceHidCompletions`
- `HaltControllerQuiet` / `ResetController` / `HaltOnly` / `BootMarkRs`
- `StartController`
- `TakeLegacy`
- `KbdPush`

### 4.2 XhciPort.c

- `PortReg` / `PortSpeed` / `PortscNeutral` / `PortscClearChange`
- `PowerConnectedPorts` / `ResetPortEx` / `ResetPort`

### 4.3 XhciDevice.c

- `Ep0RingForSlot` / `Ep0RingForSlotOut`
- `AddressDeviceOnPort` / `AddressDevice`
- `DisableSlot` / `RecoverEp0`
- `ControlXfer` / `GetDesc` / `GetDeviceDesc`
- `SetConfig` / `SetInterface` / `SetIdle` / `SetProtocolBoot` / `SetReportOutput`
- `EvaluateEp0` / `EvaluateHubSlot` / `HubNoteMttFromDevDesc`
- `SpeedMps`

### 4.4 XhciHid.c

- `ParseConfig` / `ParseConfigMouse`
- `ConfigureIntr` / `ConfigureMouseIntr`
- `QueueIntr` / `QueueMouseIntr`
- `FsInterval`
- `HidGetInputReport` / `XhciPollKbdGetReport`
- `SyncIntrDequeue`
- `PrepCompositeMouse`
- `RealPcRejectMouseExtraAsKeyboard`

### 4.5 XhciHub.c

- `HubCtrl` / `HubGetPortStatus` / `HubSetPortFeat` / `HubClearPortFeat`
- `HubPortSpeed` / `FinishHubSetup` / `IsHubDeviceDesc` / `ConfigHasHubIface`
- `ClaimHubOnRootPort` / `TryHubOnRootPort`
- `EnumHubChildrenForKeyboard` / `EnumHubChildrenForMouse`
- `TryConfigureKeyboardSlot`

### 4.6 XhciMouse.c

- `InitMouseOnPort` / `InitMouseOnKeyboardSlot` / `XhciInitMouseDeferred`
- `MousePush`
- `ClaimAddressedSlotAsMouse`
- `XhciMouseHandoffDesktop`
- `XhciDequeueMouse` / `XhciMousePresent`

### 4.7 XhciIrq.c

- `XhciIrq`
- `XhciDrainEvents`
- `XhciEnableIrq` / `XhciTryEnterDual` / `XhciFallbackToPoll`
- `XhciIrqMode` / `XhciUsesIrq`
- `EnableHostInterrupts` / `ImClearPending`

### 4.8 XhciDiag.c

- `BootLog` / `BootLogHex` / `BootLogV` / `BootLogHexV` / `BootMarkV`
- `DiagVerbose` / `DiagAppend` / `DiagChk` / `DiagChkStr`
- `EnumWhy` / `CmdTrbName`
- `XhciDiagFormat` / `XhciDiagLogArms`

### 4.9 保留在 XhciCore.c 或新建 XhciInit.c

- `XhciInit`（主初始化函数）
- `XhciAbandonNoHid`
- `XhciHidKeyboardReady`
- `XhciKeyboardSetLeds`
- `XhciDequeueKeyboard`
- `XhciMscBringUp` / `XhciMscReady` / `XhciBulkXfer`（占位）

**建议**：`XhciInit` 保留在 `XhciCore.c` 中，因为它调用所有子模块。

## 五、全局变量的定义位置

**全局变量的实际定义**放在 `XhciCore.c` 中（不是 extern），其他文件只通过 `XhciInternal.h` 的 extern 声明访问。

具体：
- 所有 `gCapabilityBase` / `gSlotId` / `gMouseSlotId` 等状态变量 → `XhciCore.c`
- 所有 TRB 环数组（`gCmdRing` / `gEp0Ring` / ...）→ `XhciCore.c`
- 所有统计计数器（`gStatIntrEvt` / ...）→ `XhciDiag.c`
- 键盘队列（`gKbdQ` / `gKeyboardWriteIndex` / ...）→ `XhciCore.c`
- 鼠标队列（`gMouseQ` / `gMouseWriteIndex` / ...）→ `XhciMouse.c`
- 鼠标缓冲（`gMouseBuf` / `gMouseAbsX` / ...）→ `XhciMouse.c`
- Hub 状态（`gHubSlotId` / `gHubRootPort` / ...）→ `XhciHub.c`
- DCBAA 缓冲（`gDcbaa` / `gDcbaaLive` / ...）→ `XhciCore.c`
- Scratchpad（`gScratchPtr` / `gScratchBuf`）→ `XhciCore.c`
- 上下文缓冲（`gDevCtx` / `gInCtx` / ...）→ `XhciCore.c`

**规则**：哪个文件的功能最相关，变量就定义在哪个文件。其他文件用 extern 引用。

## 六、Makefile 修改

找到 HAL/X64/Makefile（或主 Makefile），把：

```makefile
Drivers/XHCI.c
```

改为：

```makefile
Drivers/XHCI/XhciCore.c \
Drivers/XHCI/XhciPort.c \
Drivers/XHCI/XhciDevice.c \
Drivers/XHCI/XhciHid.c \
Drivers/XHCI/XhciHub.c \
Drivers/XHCI/XhciMouse.c \
Drivers/XHCI/XhciIrq.c \
Drivers/XHCI/XhciDiag.c
```

同时确保 include 路径包含 `Drivers/XHCI/`。

## 七、编译验证

```bash
cd ToyKernel
./build.sh
```

必须通过。如果编译报错，检查：
1. 每个 .c 文件是否 `#include "XhciInternal.h"`
2. 所有 extern 声明的变量是否在某个 .c 中有定义（不是 extern）
3. 所有共享函数是否在 XhciInternal.h 中声明
4. Makefile 是否包含所有新 .c 文件

## 八、验收标准

- [ ] HAL/X64/Drivers/XHCI/ 目录存在，包含 8 个 .c + 1 个 .h
- [ ] XHCI.h 对外接口不变
- [ ] 所有 .c 文件编译通过
- [ ] 链接通过
- [ ] QEMU 上 USB 键盘鼠标正常（`./run-split.sh`）
- [ ] 真机 NUC 上 USB 键盘鼠标正常
- [ ] 每个 .c 文件不超过 600 行

## 九、执行顺序

1. 先创建 XhciInternal.h，把所有宏、结构体、extern 变量、共享函数声明写进去
2. 然后逐个创建 .c 文件，从 XHCI.c 中把对应函数搬过去
3. 修改 Makefile
4. 编译测试
5. 如果报错，逐个修复（通常是缺少 include 或 extern 声明）

**注意**：
- 保持所有函数逻辑不变，只搬家
- 不要重命名函数
- 不要改宏定义的值
- 不要改结构体布局
- 如果遇到 static 函数被跨文件调用，改成非 static 并在 XhciInternal.h 中声明

## 十、先分析再执行

请先分析 XHCI.c，告诉我：
1. 你统计出多少个函数
2. 你打算每个文件放哪些函数
3. 有没有函数被多个模块调用，需要改成非 static
4. 有没有全局变量需要重新分配定义位置

我确认后，你再开始改代码。