/*
 * Syscall.c — 系统调用分发（入口无关）
 *
 * 教学双路径（互不耦合）：
 *   - legacy：int 0x80 → IDT 门 → InterruptDispatch → SyscallDispatch
 *   - 快速：syscall → SyscallEntry → SyscallDispatch → sysretq
 * 号与 ABI（rax / rdi,rsi,rdx）两条路径共用。
 * 向量/MSR 安装在 HalSyscallInit（PR-A1），本文件只做分发。
 * 实现：SyscallFs.c / SyscallProc.c（PR-S-syscall-split-1）。
 */
#include "Syscall.h"
#include "SyscallPriv.h"
#include "Hal.h"
#include "Console.h"
#include "Scheduler.h"
#include "Process.h"

void SyscallInit(void) {
    /* 硬件入口已由 HalSyscallInit 安装；保留符号供旧调用点 / 文档 */
}

UINT64 SyscallDispatch(HAL_INTERRUPT_FRAME *Frame) {
    UINT64 Ret = 0;

    /* 保持 IF=0 直到 iretq 恢复用户 RFLAGS，避免在返回路径嵌套定时器抢占 */
    HalIrqDisable();

    switch (HalFrameSyscallNum(Frame)) {
    case SYS_EXIT:
        Ret = SchedulerExitUser(Frame);
        break;
    case SYS_WRITE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysWrite(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            (UINTN)HalFrameGetArgument2(Frame)));
        break;
    case SYS_OPEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysOpen(HalFrameGetArgument0(Frame)));
        break;
    case SYS_READ:
        HalFrameSetReturn(Frame, (UINT64)(long)SysRead(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            (UINTN)HalFrameGetArgument2(Frame)));
        break;
    case SYS_CLOSE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysClose((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_FORK:
        Ret = SchedulerFork(Frame);
        break;
    case SYS_WAIT:
        Ret = SchedulerWait(Frame);
        break;
    case SYS_YIELD:
        Ret = SchedulerYield(Frame);
        break;
    case SYS_SOCKET:
        HalFrameSetReturn(Frame, (UINT64)(long)SysSocket(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame),
            (int)HalFrameGetArgument2(Frame)));
        break;
    case SYS_CONNECT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysConnect(
            (int)HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT16)HalFrameGetArgument2(Frame)));
        break;
    case SYS_BIND:
        HalFrameSetReturn(Frame, (UINT64)(long)SysBind(
            (int)HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT16)HalFrameGetArgument2(Frame)));
        break;
    case SYS_LISTEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysListen(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame)));
        break;
    case SYS_ACCEPT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysAccept((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_EXECVE:
        if (SysExecve(Frame, HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
                      HalFrameGetArgument2(Frame)) != 0) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        }
        /* 成功：Frame 已指向新入口，sysret 进新映像 */
        break;
    case SYS_PIPE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysPipe(HalFrameGetArgument0(Frame)));
        break;
    case SYS_DUP:
        HalFrameSetReturn(Frame, (UINT64)(long)SysDup((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_BRK:
        HalFrameSetReturn(Frame, ProcessBrk(HalFrameGetArgument0(Frame)));
        break;
    case SYS_KILL:
        Ret = SchedulerKill(Frame);
        break;
    case SYS_CREATE_WINDOW:
        HalFrameSetReturn(Frame, (UINT64)(long)SysCreateWindow(
            HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT32)HalFrameGetArgument2(Frame)));
        break;
    case SYS_DAMAGE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysDamage(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_POLL_INPUT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysPollInput(
            (int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_UI_BUTTON:
        HalFrameSetReturn(Frame, (UINT64)(long)SysUiButton(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame),
            HalFrameGetArgument2(Frame)));
        break;
    case SYS_FILE_STAT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysFileStat(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_OPEN_DIRECTORY:
        HalFrameSetReturn(Frame, (UINT64)(long)SysOpenDirectory(
            HalFrameGetArgument0(Frame)));
        break;
    case SYS_READ_DIRECTORY:
        HalFrameSetReturn(Frame, (UINT64)(long)SysReadDirectory(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_MMAP:
        HalFrameSetReturn(Frame, ProcessMmap(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            HalFrameGetArgument2(Frame)));
        break;
    case SYS_MUNMAP:
        HalFrameSetReturn(Frame, ProcessMunmap(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_SIGNAL:
        Ret = SchedulerSignal(Frame);
        break;
    default:
        ConsoleWrite("syscall: unknown ");
        ConsoleWriteHex64(HalFrameSyscallNum(Frame));
        ConsoleWrite("\n");
        HalFrameSetReturn(Frame, (UINT64)-1);
        break;
    }

    return Ret;
}
