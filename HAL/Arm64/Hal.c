/*
 * HAL/arm64/Hal.c — ARM64 HAL（PR-A7 可链接；PR-A8 virt 子集 + 轮询时钟）
 */
#include "Hal.h"

static volatile UINT64 gVirtTicks;
static UINT64 gCntLast;
static UINT64 gCntFreq;
static int gTimerReady;

int HalInit(void) {
    return 0;
}

void HalCpuHalt(void) {
    /* virt：无定时 IRQ，不能 WFI 永睡；供 Shell/Gui 任务轮询返回 */
    HalTimerPoll();
    HalCpuRelax();
}
void HalCpuPark(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}
void HalCpuReboot(void) { HalCpuPark(); }
void HalCpuShutdown(void) { HalCpuPark(); }

void HalIrqEnable(void) { }
void HalIrqDisable(void) { }
UINT64 HalIrqSave(void) {
    return 0;
}
void HalIrqRestore(UINT64 Flags) {
    (void)Flags;
}
void HalCpuRelax(void) {
    __asm__ volatile("yield" ::: "memory");
}
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
    (void)Vector; (void)Handler; (void)Type;
}
void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    (void)Vector; (void)Handler;
}
void HalIrqUnregister(UINT32 Vector) { (void)Vector; }
void HalIrqEoi(UINT32 Vector) { (void)Vector; }

void HalTimerInit(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFreq));
    if (gCntFreq == 0) {
        gCntFreq = 62500000ULL; /* QEMU virt 常见缺省 */
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gCntLast));
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) { (void)Milliseconds; }
void HalTimerAck(void) { }
void HalTimerStart(void) {
    /* A8：无 GIC 定时 IRQ，由 HalVirtIdleLoop 调 HalTimerPoll */
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Period;
    UINT64 Delta;

    if (!gTimerReady) {
        return;
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Period = gCntFreq / 100; /* ~10ms */
    if (Period == 0) {
        Period = 1;
    }
    Delta = Now - gCntLast;
    while (Delta >= Period) {
        HalCpuTickInc();
        gCntLast += Period;
        Delta -= Period;
    }
}

int HalPlatformVirtConsole(void) {
    return 1;
}

void HalVirtIdleLoop(void) {
    /* A9：正式路径走 ConsoleSerialRun；此处仅作误入 sched 的兜底 */
    HalSerialWrite("virt: idle loop (no console)\n");
    for (;;) {
        HalTimerPoll();
        HalCpuRelax();
    }
}

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
void HalSchedulerEnter(struct HAL_FRAME *Frame) {
    UINT64 Entry;
    UINT64 Stack;

    if (!Frame || Frame->Rip == 0) {
        HalVirtIdleLoop();
        return;
    }
    Entry = Frame->Rip;
    Stack = Frame->Rsp;
    /* 首次进入内核任务：切栈并跳到 Entry（无返回；无抢占则该任务一直跑） */
    __asm__ volatile(
        "mov sp, %0\n"
        "br  %1\n"
        :
        : "r"(Stack), "r"(Entry)
        : "memory");
    HalVirtIdleLoop();
}
void HalUserEnter(struct HAL_FRAME *Frame) { (void)Frame; }

/* 分页实现见 Page.c（PR-A7） */

const char *HalArchName(void) { return "aarch64"; }
const char *HalCpuInfo(void) { return "ARM64 (virt A8)"; }

UINT16 HalElfMachine(void) {
    return 183; /* EM_AARCH64 */
}

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type) {
    (void)Type;
    return HAL_ELF_RELOC_UNSUPPORTED;
}

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
UINT64 HalCpuTicks(UINT32 Cpu) {
    (void)Cpu;
    return gVirtTicks;
}
void HalCpuTickInc(void) {
    gVirtTicks++;
}
int HalSmpStartAps(void) { return 0; }
