/*
 * Include/Hal.h — 硬件抽象层统一接口
 */
#ifndef HAL_H
#define HAL_H

#include "BootTypes.h"
#include "HalPort.h"

/* 页表权限（架构无关语义，x86 位布局见 Page.c） */
#define HAL_PAGE_PRESENT  (1ULL << 0)
#define HAL_PAGE_WRITABLE (1ULL << 1)
#define HAL_PAGE_USER     (1ULL << 2)

void HalPlatformMapMmio(void);
UINT64 HalPlatformXhciFallback(void);

typedef void *(*HalPageAllocateFunction)(void *Ctx);

int HalInit(void);

void HalCpuHalt(void);
void HalCpuPark(void);
void HalCpuReboot(void);
void HalCpuShutdown(void);

void HalIrqEnable(void);
void HalIrqDisable(void);
void HalIdtSetGate(UINT32 Vector, void *Handler, UINT8 Type);
void HalIrqRegister(UINT32 Vector, void (*Handler)(void));
void HalIrqUnregister(UINT32 Vector);
void HalIrqEoi(UINT32 Vector);

void HalTimerInit(void);
void HalTimerSetInterval(UINT32 Milliseconds);
void HalTimerAck(void);
void HalTimerStart(void);

void HalUserInstall(void);
void HalSyscallInit(void);

struct HAL_FRAME;
UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame);
void HalSchedulerEnter(struct HAL_FRAME *Frame);
void HalUserEnter(struct HAL_FRAME *Frame);

/* 端口 / 早期 I/O（x86 为 in/out；其它架构可为空操作或 MMIO 映射） */
UINT8  HalIoRead8(UINT16 Port);
UINT16 HalIoRead16(UINT16 Port);
UINT32 HalIoRead32(UINT16 Port);
void   HalIoWrite8(UINT16 Port, UINT8 Value);
void   HalIoWrite16(UINT16 Port, UINT16 Value);
void   HalIoWrite32(UINT16 Port, UINT32 Value);

/* 分页：CPU 寄存器 */
void HalFlushTlb(UINT64 VirtualAddress);
void HalLoadPageTable(UINT64 Cr3);
UINT64 HalGetPageTable(void);
void HalPagingEnable(UINT64 RootPhys);

/* 分页：页表结构 */
int HalPageKernelSetup(UINTN IdentityMegabytes);
UINT64 HalPageKernelRoot(void);
UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx);
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot);
int HalPagePrivatizePml4Slot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx);
int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx);
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End);
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt);
UINT64 HalPageGetEntryCurrent(UINT64 Virt);

void HalConsolePutChar(char C);
char HalConsoleGetChar(void);
int HalConsoleHasChar(void);

const char *HalArchName(void);
const char *HalCpuInfo(void);

#endif
