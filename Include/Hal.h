/*
 * Include/Hal.h — 硬件抽象层统一接口
 */
#ifndef HAL_H
#define HAL_H

#include "BootTypes.h"
#include "HalPort.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "HalConsole.h"
#include "HalDevices.h"

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
/* PR-A5：保存并关中断；Restore 恢复进入时的中断状态（可嵌套调用方自管） */
UINT64 HalIrqSave(void);
void HalIrqRestore(UINT64 Flags);
void HalCpuRelax(void); /* 自旋等待提示（x86：pause） */
/* Type：架构相关门描述（x86 为 IDT type；其它架构可忽略） */
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type);
void HalIrqRegister(UINT32 Vector, void (*Handler)(void));
void HalIrqUnregister(UINT32 Vector);
void HalIrqEoi(UINT32 Vector);

void HalTimerInit(void);
void HalTimerSetInterval(UINT32 Milliseconds);
void HalTimerAck(void);
void HalTimerStart(void);

void HalUserInstall(void);
void HalSyscallInit(void);
/* 用户任务内核栈顶 → TSS.RSP0 / 等价结构（PR-A1；取代 Common 调 ArchSetRsp0） */
void HalSetKernelStack(UINT64 StackTop);

/*
 * PR-A2：可移植任务帧 / 系统调用 ABI。
 * Common 用这些 API，不直接写 Cs/Ss/Rax/Rip 或 GDT 选择子字面量。
 */
void HalFrameSetKernelEntry(HAL_FRAME *F, UINT64 Entry, UINT64 StackTop);
void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserRsp);
void HalFrameCopy(HAL_FRAME *Dst, const HAL_FRAME *Src);
UINT64 HalFrameGetRip(const HAL_FRAME *F);
UINT64 HalFrameSyscallNum(const HAL_FRAME *F);
UINT64 HalFrameArg0(const HAL_FRAME *F);
UINT64 HalFrameArg1(const HAL_FRAME *F);
UINT64 HalFrameArg2(const HAL_FRAME *F);
void HalFrameSetReturn(HAL_FRAME *F, UINT64 Value);
void HalFrameSetReturn2(HAL_FRAME *F, UINT64 A, UINT64 B);

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

/* 分页：CPU 当前页表根（x86 为 CR3；其它架构为等价寄存器） */
void HalFlushTlb(UINT64 VirtualAddress);
void HalLoadPageTable(UINT64 Root);
UINT64 HalGetPageTable(void);
void HalPagingEnable(UINT64 RootPhys);

/* 分页：页表结构 */
int HalPageKernelSetup(UINTN IdentityMegabytes);
UINT64 HalPageKernelRoot(void);
UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx);
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot);
/* 将根表槽 Index 换成私有下一级表（x86：PML4→PDPT）；fork 浅拷贝后必须私有化 */
int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx);
/*
 * PR-A3：用户地址空间根表准备（x86：私有化槽 0 / PDPT）。
 * Common 不写死根槽号或 PML4[0]。
 */
int HalPagePrepareUserRoot(UINT64 Root, HalPageAllocateFunction Alloc, void *Ctx);
/* PR-A3：COW 软件语义（x86 用 PTE 可用位 bit9；其它 arch 自选布局） */
int HalPageIsCow(UINT64 Pte);
UINT64 HalPageMarkCow(UINT64 Flags); /* 置 COW、清 WRITABLE */
int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx);
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End);
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt);
UINT64 HalPageGetEntryCurrent(UINT64 Virt);

/* PR-A4：ELF 机器号与重定位分类（Common 不写死 EM_X86_64 / R_X86_64_*） */
UINT16 HalElfMachine(void);

typedef enum {
    HAL_ELF_RELOC_UNSUPPORTED = 0,
    HAL_ELF_RELOC_RELATIVE,
    HAL_ELF_RELOC_ABS64,
    HAL_ELF_RELOC_GLOB_DAT,
    HAL_ELF_RELOC_JUMP_SLOT,
    HAL_ELF_RELOC_COPY
} HAL_ELF_RELOC_KIND;

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type);

const char *HalArchName(void);
const char *HalCpuInfo(void);

/* SMP：Common 只依赖这些门面；x86=MADT/SIPI，其它架构 stub */
#define HAL_MAX_CPUS 8

int HalCpuCount(void);
UINT32 HalCpuId(void);          /* 逻辑 CPU：0=BSP，1..N-1=AP */
int HalCpuIsBsp(void);
UINT64 HalCpuTicks(UINT32 Cpu); /* 每核 LAPIC timer 计数（PR-S2） */
void HalCpuTickInc(void);
int HalSmpStartAps(void);

void HalDebugWrite(const char *Text);
void HalDebugHex32(UINT32 Value);
void HalDebugHex64(UINT64 Value);

#endif
