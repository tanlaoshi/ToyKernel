/*
 * HAL/x86_64/Hal.c — x86-64 HAL 实现（委托给 Arch / VirtualMemory / Serial）
 */
#include "Hal.h"
#include "Arch.h"
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

void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type) {
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

const char *HalArchName(void) {
    return "x86_64";
}

const char *HalCpuInfo(void) {
    return "x86-64 (ToyOS HAL)";
}

void HalDebugWrite(const char *Text) {
    HalSerialWrite(Text);
}

void HalDebugHex32(UINT32 Value) {
    char Buf[12];

    HalSerialHexFormat(Buf, Value, 8);
    HalSerialWrite(Buf);
}

void HalDebugHex64(UINT64 Value) {
    char Buf[20];

    HalSerialHexFormat(Buf, Value, 16);
    HalSerialWrite(Buf);
}

void HalCpuPark(void) {
    __asm__ volatile ("cli; hlt");
}
