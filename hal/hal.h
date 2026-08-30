/*
 * hal/hal.h — 硬件抽象层统一接口
 */
#ifndef HAL_H
#define HAL_H

#include "BootConfig.h"
#include "hal_port.h"

/* 页表权限（架构无关语义，x86 位布局见 hal/$(ARCH)/Page.c） */
#define HAL_PAGE_PRESENT  (1ULL << 0)
#define HAL_PAGE_WRITABLE (1ULL << 1)
#define HAL_PAGE_USER     (1ULL << 2)

typedef void *(*HalPageAllocFn)(void *Ctx);

int HalInit(BOOT_CONFIG *Config);

void HalCpuHalt(void);
void HalCpuPark(void);
void HalCpuReboot(void);
void HalCpuShutdown(void);

void HalIrqEnable(void);
void HalIrqDisable(void);
void HalIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type);
void HalIrqRegister(UINT32 Vector, void (*Handler)(void));
void HalIrqUnregister(UINT32 Vector);
void HalIrqEoi(UINT32 Vector);

void HalTimerInit(void);
void HalTimerSetInterval(UINT32 Millisec);
void HalTimerAck(void);
void HalTimerStart(void);

void HalUserInstall(void);
void HalSyscallInit(void);

struct HAL_FRAME;
UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame);
void HalSchedEnter(struct HAL_FRAME *Frame);
void HalUserEnter(struct HAL_FRAME *Frame);

/* 分页：CPU 寄存器 */
void HalFlushTlb(UINT64 Virt);
void HalLoadPageTable(UINT64 Cr3);
UINT64 HalGetPageTable(void);
void HalPagingEnable(UINT64 RootPhys);

/* 分页：页表结构（第 2 步） */
int HalPageKernelSetup(UINTN IdentityMb);
UINT64 HalPageKernelRoot(void);
UINT64 HalPageRootCreate(HalPageAllocFn Alloc, void *Ctx);
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot);
int HalPageMap(UINT64 Root, UINT64 Virt, UINT64 Phys, UINT64 Flags,
               HalPageAllocFn Alloc, void *Ctx);
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End);
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt);
UINT64 HalPageGetEntryCurrent(UINT64 Virt);

void HalConsolePutChar(char C);
char HalConsoleGetChar(void);
int HalConsoleHasChar(void);

const char *HalArchName(void);
const char *HalCpuInfo(void);

#endif
