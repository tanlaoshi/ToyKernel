/*
 * HAL/riscv/Hal.c — RISC-V HAL（PR-A7 可链接；PR-A8 virt 子集 + 轮询时钟）
 */
#include "Hal.h"

/* QEMU virt CLINT mtime；默认约 10MHz */
#define CLINT_MTIME 0x0200bff8ULL
#define MTIME_HZ    10000000ULL

static volatile UINT64 gVirtTicks;
static UINT64 gMtimeLast;
static int gTimerReady;

static UINT64 ReadMtime(void) {
    return *(volatile UINT64 *)(UINTN)CLINT_MTIME;
}

int HalInit(void) {
    return 0;
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
UINT64 HalIrqSave(void) {
    return 0;
}
void HalIrqRestore(UINT64 Flags) {
    (void)Flags;
}
void HalCpuRelax(void) {
    __asm__ volatile("" ::: "memory");
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
    gMtimeLast = ReadMtime();
    gTimerReady = 1;
}

void HalTimerSetInterval(UINT32 Milliseconds) { (void)Milliseconds; }
void HalTimerAck(void) { }
void HalTimerStart(void) {
    /* A8：无 PLIC/CLINT IRQ，由 HalVirtIdleLoop 调 HalTimerPoll */
}

void HalTimerPoll(void) {
    UINT64 Now;
    UINT64 Period;
    UINT64 Delta;

    if (!gTimerReady) {
        return;
    }
    Now = ReadMtime();
    Period = MTIME_HZ / 100; /* ~10ms */
    if (Period == 0) {
        Period = 1;
    }
    Delta = Now - gMtimeLast;
    while (Delta >= Period) {
        HalCpuTickInc();
        gMtimeLast += Period;
        Delta -= Period;
    }
}

int HalPlatformVirtConsole(void) {
    return 1;
}

void HalVirtIdleLoop(void) {
    HalSerialWrite("virt: idle loop (serial poll)\n");
    for (;;) {
        HalTimerPoll();
        if (HalSerialDataReady()) {
            (void)HalSerialReadChar(); /* A9：再接到命令解析 */
        }
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
    (void)Frame;
    HalVirtIdleLoop();
}
void HalUserEnter(struct HAL_FRAME *Frame) { (void)Frame; }

/* 分页实现见 Page.c（PR-A7） */

const char *HalArchName(void) { return "riscv64"; }
const char *HalCpuInfo(void) { return "RISC-V (virt A8)"; }

UINT16 HalElfMachine(void) {
    return 243; /* EM_RISCV */
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
