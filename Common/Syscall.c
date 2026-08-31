/*
 * Syscall.c — int 0x80 系统调用（用户 Ring 3 可触发）
 */
#include "Syscall.h"
#include "hal.h"
#include "Console.h"
#include "UI.h"
#include "Video.h"
#include "Scheduler.h"
#include "Serial.h"
#include "Debug.h"
#include "VirtualMemory.h"

extern void Isr128(void);

#define COPY_BUF_MAX 256

void SyscallInit(void) {
    HalIdtSetGate(VEC_SYSCALL, (void *)Isr128, 0xEE);
    DebugWrite("syscall: vector 0x80 (DPL=3) ready\n");
}

static int SysWrite(int Fd, UINT64 UserBuf, UINTN Len) {
    char Buf[COPY_BUF_MAX];
    UINTN Done = 0;

    (void)Fd;
    while (Done < Len) {
        UINTN Chunk = Len - Done;
        if (Chunk > COPY_BUF_MAX) {
            Chunk = COPY_BUF_MAX;
        }
        if (VirtualMemoryCopyFromUser(Buf, UserBuf + Done, Chunk) < 0) {
            return Done > 0 ? (int)Done : -1;
        }
        for (UINTN i = 0; i < Chunk; i++) {
            char C[2] = {Buf[i], 0};
            SerialWrite(C);
            VideoDrawChar(C[0], COLOR_WHITE);
        }
        Done += Chunk;
    }
    return (int)Len;
}

UINT64 SyscallDispatch(struct INT_FRAME *Frame) {
    switch (Frame->Rax) {
    case SYS_EXIT:
        return SchedulerExitUser(Frame);
    case SYS_WRITE:
        Frame->Rax = (UINT64)(long)SysWrite(
            (int)Frame->Rdi, Frame->Rsi, (UINTN)Frame->Rdx);
        return 0;
    default:
        ConsoleWrite("syscall: unknown ");
        ConsoleHex64(Frame->Rax);
        ConsoleWrite("\n");
        Frame->Rax = (UINT64)-1;
        return 0;
    }
}
