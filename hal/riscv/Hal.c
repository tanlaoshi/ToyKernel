/*
 * hal/riscv/Hal.c — RISC-V HAL 占位（未链接进当前内核）
 */
#include "hal.h"

int HalInit(BOOT_CONFIG *Config) {
    (void)Config;
    return -1;
}

void HalCpuHalt(void) { for (;;) { } }
void HalCpuPark(void) { for (;;) { } }
void HalCpuReboot(void) { HalCpuPark(); }
void HalCpuShutdown(void) { HalCpuPark(); }

void HalIrqEnable(void) { }
void HalIrqDisable(void) { }
void HalIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type) {
    (void)Vec; (void)Handler; (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) { (void)Vector; }
void HalIrqEoi(UINT32 Vector) { (void)Vector; }

void HalTimerInit(void) { }
void HalTimerSetInterval(UINT32 Millisec) { (void)Millisec; }
void HalTimerAck(void) { }
void HalTimerStart(void) { }

void HalUserInstall(void) { }
void HalSyscallInit(void) { }

UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame) {
    (void)Frame;
    return 0;
}
void HalSchedEnter(struct HAL_FRAME *Frame) { (void)Frame; }
void HalUserEnter(struct HAL_FRAME *Frame) { (void)Frame; }

void HalFlushTlb(UINT64 Virt) { (void)Virt; }
void HalLoadPageTable(UINT64 Cr3) { (void)Cr3; }
UINT64 HalGetPageTable(void) { return 0; }
void HalPagingEnable(UINT64 RootPhys) { (void)RootPhys; }

int HalPageKernelSetup(UINTN IdentityMb) { (void)IdentityMb; return -1; }
UINT64 HalPageKernelRoot(void) { return 0; }
UINT64 HalPageRootCreate(HalPageAllocFn Alloc, void *Ctx) {
    (void)Alloc; (void)Ctx; return 0;
}
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot) {
    (void)DstRoot; (void)SrcRoot;
}
int HalPageMap(UINT64 Root, UINT64 Virt, UINT64 Phys, UINT64 Flags,
               HalPageAllocFn Alloc, void *Ctx) {
    (void)Root; (void)Virt; (void)Phys; (void)Flags; (void)Alloc; (void)Ctx;
    return -1;
}
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End) {
    (void)Root; (void)Start; (void)End; return 0;
}
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt) {
    (void)Root; (void)Virt; return 0;
}
UINT64 HalPageGetEntryCurrent(UINT64 Virt) { (void)Virt; return 0; }

void HalConsolePutChar(char C) { (void)C; }
char HalConsoleGetChar(void) { return 0; }
int HalConsoleHasChar(void) { return 0; }

const char *HalArchName(void) { return "riscv64"; }
const char *HalCpuInfo(void) { return "RISC-V (stub)"; }
