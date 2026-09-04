/*
 * HAL/arm64/Hal.c — ARM64 HAL 占位（未链接进当前内核）
 */
#include "Hal.h"

int HalInit(void) {
    return -1;
}

void HalCpuHalt(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}
void HalCpuPark(void) { HalCpuHalt(); }
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

void HalFrameSetKernelEntry(HAL_FRAME *F, UINT64 Entry, UINT64 StackTop) {
    UINTN j;
    if (!F) {
        return;
    }
    for (j = 0; j < sizeof(HAL_FRAME); j++) {
        ((UINT8 *)F)[j] = 0;
    }
    F->Rip = Entry;
    F->Rsp = StackTop;
}

void HalFrameSetUserEntry(HAL_FRAME *F, UINT64 Entry, UINT64 UserRsp) {
    HalFrameSetKernelEntry(F, Entry, UserRsp);
}

void HalFrameCopy(HAL_FRAME *Dst, const HAL_FRAME *Src) {
    UINTN j;
    if (!Dst || !Src) {
        return;
    }
    for (j = 0; j < sizeof(HAL_FRAME); j++) {
        ((UINT8 *)Dst)[j] = ((const UINT8 *)Src)[j];
    }
}

UINT64 HalFrameGetRip(const HAL_FRAME *F) {
    return F ? F->Rip : 0;
}

UINT64 HalFrameSyscallNum(const HAL_FRAME *F) {
    (void)F;
    return 0;
}

UINT64 HalFrameArg0(const HAL_FRAME *F) {
    (void)F;
    return 0;
}

UINT64 HalFrameArg1(const HAL_FRAME *F) {
    (void)F;
    return 0;
}

UINT64 HalFrameArg2(const HAL_FRAME *F) {
    (void)F;
    return 0;
}

void HalFrameSetReturn(HAL_FRAME *F, UINT64 Value) {
    (void)F;
    (void)Value;
}

void HalFrameSetReturn2(HAL_FRAME *F, UINT64 A, UINT64 B) {
    (void)F;
    (void)A;
    (void)B;
}

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

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) { return "ARM64 (virt bringup)"; }

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}
void HalDebugHex32(UINT32 Value) {
    char Buf[9];
    HalSerialHexFormat(Buf, Value, 8);
    HalSerialWrite(Buf);
}
void HalDebugHex64(UINT64 Value) {
    char Buf[17];
    HalSerialHexFormat(Buf, Value, 16);
    HalSerialWrite(Buf);
}

int HalCpuCount(void) { return 1; }
UINT32 HalCpuId(void) { return 0; }
int HalCpuIsBsp(void) { return 1; }
UINT64 HalCpuTicks(UINT32 Cpu) { (void)Cpu; return 0; }
void HalCpuTickInc(void) { }
int HalSmpStartAps(void) { return 0; }
