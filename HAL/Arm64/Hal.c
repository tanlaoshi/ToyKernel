/*
 * HAL/arm64/Hal.c — ARM64 HAL 占位（未链接进当前内核）
 */
#include "Hal.h"

int HalInit(void) {
    return -1;
}

void HalCpuHalt(void) { for (;;) { } }
void HalCpuPark(void) { for (;;) { } }
void HalCpuReboot(void) { HalCpuPark(); }
void HalCpuShutdown(void) { HalCpuPark(); }

void HalIrqEnable(void) { }
void HalIrqDisable(void) { }
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    (void)Vector; (void)Handler; (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) { (void)Vector; }
void HalIrqEoi(UINT32 Vector) { (void)Vector; }

void HalTimerInit(void) { }
void HalTimerSetInterval(UINT32 Milliseconds) { (void)Milliseconds; }
void HalTimerAck(void) { }
void HalTimerStart(void) { }

void HalUserInstall(void) { }
void HalSyscallInit(void) { }
void HalSetKernelStack(UINT64 StackTop) { (void)StackTop; }

UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame) {
    (void)Frame;
    return 0;
}
void HalSchedulerEnter(struct HAL_FRAME *Frame) { (void)Frame; }
void HalUserEnter(struct HAL_FRAME *Frame) { (void)Frame; }

void HalFlushTlb(UINT64 VirtualAddress) { (void)VirtualAddress; }
void HalLoadPageTable(UINT64 Root) { (void)Root; }
UINT64 HalGetPageTable(void) { return 0; }
void HalPagingEnable(UINT64 RootPhys) { (void)RootPhys; }

int HalPageKernelSetup(UINTN IdentityMegabytes) { (void)IdentityMegabytes; return -1; }
UINT64 HalPageKernelRoot(void) { return 0; }
UINT64 HalPageRootCreate(HalPageAllocateFunction Alloc, void *Ctx) {
    (void)Alloc; (void)Ctx; return 0;
}
void HalPageRootCopy(UINT64 DstRoot, UINT64 SrcRoot) {
    (void)DstRoot; (void)SrcRoot;
}
int HalPagePrivatizeRootSlot(UINT64 Root, UINT32 Index, HalPageAllocateFunction Alloc, void *Ctx) {
    (void)Root; (void)Index; (void)Alloc; (void)Ctx; return -1;
}
int HalPageMap(UINT64 Root, UINT64 VirtualAddress, UINT64 PhysicalAddress, UINT64 Flags,
               HalPageAllocateFunction Alloc, void *Ctx) {
    (void)Root; (void)VirtualAddress; (void)PhysicalAddress; (void)Flags; (void)Alloc; (void)Ctx;
    return -1;
}
int HalPageUnmapRange(UINT64 Root, UINT64 Start, UINT64 End) {
    (void)Root; (void)Start; (void)End; return 0;
}
UINT64 HalPageGetEntry(UINT64 Root, UINT64 Virt) {
    (void)Root; (void)Virt; return 0;
}
UINT64 HalPageGetEntryCurrent(UINT64 Virt) { (void)Virt; return 0; }

void HalSerialInit(void) { }
void HalSerialWrite(const char *Text) { (void)Text; }
int HalSerialDataReady(void) { return 0; }
char HalSerialReadChar(void) { return 0; }
void HalSerialHexFormat(char *Buf, UINT64 Value, int Digits) {
    (void)Buf; (void)Value; (void)Digits;
}

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) { return "ARM64 (stub)"; }

void HalDebugWrite(const char *Text) { (void)Text; }
void HalDebugHex32(UINT32 Value) { (void)Value; }
void HalDebugHex64(UINT64 Value) { (void)Value; }

int HalCpuCount(void) { return 1; }
UINT32 HalCpuId(void) { return 0; }
int HalCpuIsBsp(void) { return 1; }
UINT64 HalCpuTicks(UINT32 Cpu) { (void)Cpu; return 0; }
void HalCpuTickInc(void) { }
int HalSmpStartAps(void) { return 0; }
