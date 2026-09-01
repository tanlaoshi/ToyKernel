/*
 * HAL/x86_64/Hal.c — x86-64 HAL 实现（委托给 Arch / VirtualMemory / Serial）
 */
#include "Hal.h"
#include "Arch.h"
#include "Serial.h"
#include "Syscall.h"
#include "Debug.h"

int HalInit(void) {
    return ArchInit();
}

void HalCpuHalt(void) {
    __asm__ volatile ("sti; hlt; cli" ::: "memory");
}

void HalCpuReboot(void) {
    for (;;) {
        HalCpuHalt();
    }
}

void HalCpuShutdown(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void HalIrqEnable(void) {
    ArchSti();
}

void HalIrqDisable(void) {
    ArchCli();
}

void HalIdtSetGate(UINT32 Vector, void *Handler, UINT8 Type) {
    ArchIdtSetGate(Vector, Handler, Type);
}

void HalIrqRegister(UINT32 Vector, void (*Handler)(void)) {
    ArchIdtSetGate(Vector, Handler, 0x8E);
}

void HalIrqUnregister(UINT32 Vector) {
    ArchIdtSetGate(Vector, (void *)0, 0);
    (void)Vector;
}

void HalIrqEoi(UINT32 Vector) {
    (void)Vector;
    LapicEoi();
}

void HalTimerInit(void) {
}

void HalTimerSetInterval(UINT32 Milliseconds) {
    (void)Milliseconds;
}

void HalTimerAck(void) {
    LapicEoi();
}

void HalTimerStart(void) {
    TimerStart();
}

void HalUserInstall(void) {
    ArchTssInstall();
}

void HalSyscallInit(void) {
    SyscallInit();
}

UINT64 HalInterruptDispatch(struct HAL_FRAME *Frame) {
    return InterruptDispatch(Frame);
}

void HalSchedulerEnter(struct HAL_FRAME *Frame) {
    SchedulerEnter(Frame);
}

void HalUserEnter(struct HAL_FRAME *Frame) {
    UserEnter(Frame);
}

void HalConsolePutChar(char C) {
#if TOY_DEBUG
    char Buf[2] = {C, 0};
    DebugWrite(Buf);
#else
    (void)C;
#endif
}

char HalConsoleGetChar(void) {
    while (!SerialDataReady()) {
        HalCpuHalt();
    }
    return SerialReadChar();
}

int HalConsoleHasChar(void) {
    return SerialDataReady();
}

const char *HalArchName(void) {
    return "x86_64";
}

const char *HalCpuInfo(void) {
    return "x86-64 (ToyOS HAL)";
}

void HalDebugWrite(const char *Text) {
    SerialWrite(Text);
}

void HalDebugHex32(UINT32 Value) {
    SerialHex32(Value);
}

void HalDebugHex64(UINT64 Value) {
    SerialHex64(Value);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}
