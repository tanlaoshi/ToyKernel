/*
 * ShellCommandsThread.c — PR-U-thread：test thread（栈+TLS+同 CR3）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Scheduler.h"

static int ParseDecInt(const char *S, INT32 *Out) {
    INT32 V = 0;

    if (!S || !S[0] || !Out) {
        return -1;
    }
    while (*S) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10 + (*S - '0');
        S++;
    }
    *Out = V;
    return 0;
}

static void WriteSpinErr(int Err) {
    ConsoleWrite("test thread: fail err=");
    ConsoleWriteHex32((UINT32)Err);
    ConsoleWrite(" (1=arg 2=busy 3=nomem 4=nova 5=map 6=create)\n");
    if (Err == 2) {
        ConsoleWrite("  hint: TOY_SMP=1 + SNAKE；串口敲命令（0 非法会变成双核）\n");
    }
}

static void CommandTestThread(int Argc, char **Argv) {
    INT32 Pid;
    const TASK *T;
    TASK *Leader;
    int Slot;
    int i;
    int Same;
    UINT64 LeaderTls;

    if (Argc < 2) {
        ConsoleWrite("usage: test thread <pid>\n");
        ConsoleWrite("  TOY_SMP=1；ps 十进制 pid；SNAKE 挂住后测\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0 || Pid > MAX_TASKS) {
        ConsoleWrite("test thread: bad pid\n");
        return;
    }
    T = SchedulerTaskByIndex(Pid - 1);
    if (!T || !T->IsUser || T->State == TASK_UNUSED) {
        ConsoleWrite("test thread: not a live user task\n");
        return;
    }
    Leader = (TASK *)(UINTN)T;
    (void)SchedulerThreadEnsureTls(Leader);
    LeaderTls = Leader->TlsBase;
    Slot = SchedulerCreateThreadSpin(Leader);
    if (Slot < 0) {
        WriteSpinErr(-Slot);
        return;
    }
    Same = 1;
    for (i = 0; i < MAX_TASKS; i++) {
        const TASK *U = SchedulerTaskByIndex(i);
        if (!U || !U->IsUser || U->State == TASK_UNUSED) {
            continue;
        }
        if (U->GroupId != Leader->GroupId) {
            continue;
        }
        if (U->PageRoot != Leader->PageRoot || U->UserSpace != Leader->UserSpace) {
            Same = 0;
        }
        ConsoleWrite("  tid=");
        ConsoleWriteHex32((UINT32)U->Id);
        ConsoleWrite(" thr=");
        ConsoleWriteHex32((UINT32)U->IsThread);
        ConsoleWrite(" tls=");
        ConsoleWriteHex64(U->TlsBase);
        ConsoleWrite(" root=");
        ConsoleWriteHex64(U->PageRoot);
        ConsoleWrite("\n");
    }
    ConsoleWrite("test thread: twin slot=");
    ConsoleWriteHex32((UINT32)Slot);
    if (Same) {
        ConsoleWrite(" same-CR3 ok");
    } else {
        ConsoleWrite(" CR3 mismatch");
    }
    {
        const TASK *Twin = SchedulerTaskByIndex(Slot);
        if (LeaderTls != 0 && Twin && Twin->TlsBase != 0 && LeaderTls != Twin->TlsBase) {
            ConsoleWrite(" tls-distinct ok");
        } else {
            ConsoleWrite(" tls-check fail");
        }
    }
    ConsoleWrite(" (parked)\n");
    SchedulerDropDiagThread(Slot);
}

void ShellCommandsThreadRegister(void) {
    ConsoleRegister2("test", "thread", "spawn shared-VAS spin twin", CommandTestThread);
}
