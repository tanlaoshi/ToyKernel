/*
 * ShellCommandsSystemProc.c — PR-S3-shellsys-1：exec / ps / kill / set priority / runuser
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Process.h"
#include "Scheduler.h"
#include "Syscall.h"
#include "Tasks.h"

static void CommandRunuser(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    if (ProcessRunDemo() == 0) {
        ConsoleWaitPrompt();
    }
}

static void CommandExec(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: exec <file>\n");
        return;
    }
    /*
     * 先占 stdin，再起用户态：避免 ProcessExec 返回前 GUI/Shell 已抢串口。
     * virt 协作排空：exec 返回时进程已结束，立刻 ShowPrompt 还原。
     */
    if (!HalPlatformIsVirtSerialConsole()) {
        ConsoleWaitPrompt();
    }
    if (ProcessExec(Argv[1]) != 0) {
        if (!HalPlatformIsVirtSerialConsole()) {
            ConsoleShowPrompt();
        }
        return;
    }
    if (HalPlatformIsVirtSerialConsole()) {
        return;
    }
    /* 已 WaitPrompt；exit 时 ShowPrompt */
}

static void CommandPs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    for (int i = 0; i < MAX_TASKS; i++) {
        const TASK *T = SchedulerTaskByIndex(i);
        if (!T) {
            continue;
        }
        ConsoleWrite("  pid=");
        ConsoleWriteHex32((UINT32)(i + 1));
        ConsoleWrite(" ");
        ConsoleWrite(T->Name);
        if (T->IsUser) {
            ConsoleWrite(" user");
        } else {
            ConsoleWrite(" kern");
        }
        if (T->State == TASK_ZOMBIE) {
            ConsoleWrite(" zombie");
        } else if (T->State == TASK_BLOCKED) {
            ConsoleWrite(" blocked");
        }
        ConsoleWrite(" root=");
        ConsoleWriteHex64(T->PageRoot);
        ConsoleWrite(" rip=");
        ConsoleWriteHex64(SchedulerTaskRip(T));
        ConsoleWrite(" ticks=");
        ConsoleWriteHex32(T->Ticks);
        ConsoleWrite(" cpu=");
        ConsoleWriteHex32((UINT32)T->OnCpu);
        ConsoleWrite(" home=");
        ConsoleWriteHex32((UINT32)T->HomeCpu);
        ConsoleWrite(" prio=");
        if (T->Priority < 0) {
            ConsoleWrite("-");
            ConsoleWriteHex32((UINT32)(-T->Priority));
        } else {
            ConsoleWriteHex32((UINT32)T->Priority);
        }
        if (SchedulerCurrent() == T) {
            ConsoleWrite(" *");
        }
        ConsoleWrite("\n");
    }
    ConsoleWrite("cpu ticks=");
    ConsoleWriteHex64(HalCpuTicks(0));
    ConsoleWrite(" worker loops=");
    ConsoleWriteHex32(WorkerLoopCount());
    ConsoleWrite(" steals=");
    ConsoleWriteHex64(SchedulerStealCount());
    ConsoleWrite("\n");
}

static int ParseDecInt(const char *S, INT32 *Out) {
    INT32 V = 0;
    int Neg = 0;
    if (!S || !S[0] || !Out) {
        return -1;
    }
    if (*S == '-') {
        Neg = 1;
        S++;
        if (!*S) {
            return -1;
        }
    }
    for (; *S; S++) {
        if (*S < '0' || *S > '9') {
            return -1;
        }
        V = V * 10 + (*S - '0');
    }
    *Out = Neg ? -V : V;
    return 0;
}

/* PR-S-lock：set priority <pid> <prio>；pid 与 list tasks 一致 */
static void CommandSetPriority(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Priority = 0;

    if (Argc < 3) {
        ConsoleWrite("usage: set priority <pid> <prio>\n");
        ConsoleWrite("  prio: -128..127 (higher runs sooner; shell/gui default 8)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        ConsoleWrite("set priority: bad pid\n");
        return;
    }
    if (ParseDecInt(Argv[2], &Priority) != 0) {
        ConsoleWrite("set priority: bad prio\n");
        return;
    }
    if (SchedulerSetPriority(Pid, Priority) != 0) {
        ConsoleWrite("set priority: fail\n");
        return;
    }
    ConsoleWrite("set priority: pid=");
    ConsoleWriteHex32((UINT32)Pid);
    ConsoleWrite(" prio=");
    if (Priority < 0) {
        ConsoleWrite("-");
        ConsoleWriteHex32((UINT32)(-Priority));
    } else {
        ConsoleWriteHex32((UINT32)Priority);
    }
    ConsoleWrite("\n");
}

/* PR-P4：kill <pid> [sig]；pid 与 ps / fork 一致（槽位+1）；默认 SIGTERM */
static void CommandKill(int Argc, char **Argv) {
    INT32 Pid = 0;
    INT32 Sig = SIGTERM;

    if (Argc < 2) {
        ConsoleWrite("usage: kill <pid> [sig]\n");
        ConsoleWrite("  sig: 2=INT 9=KILL 15=TERM (default)\n");
        return;
    }
    if (ParseDecInt(Argv[1], &Pid) != 0 || Pid <= 0) {
        ConsoleWrite("kill: bad pid\n");
        return;
    }
    if (Argc >= 3) {
        if (ParseDecInt(Argv[2], &Sig) != 0) {
            ConsoleWrite("kill: bad sig\n");
            return;
        }
    }
    if (SchedulerKillPid(Pid, Sig) != 0) {
        ConsoleWrite("kill: failed (user only; INT/KILL/TERM)\n");
        return;
    }
    ConsoleWrite("kill: ok\n");
}

void ShellCommandsSystemProcRegisterVirtMin(void) {
    ConsoleRegister2("list", "tasks", "list tasks", CommandPs);
    ConsoleRegisterAliasLine("ps", "list", "tasks");
    ConsoleRegister("execute", "load ELF (TOYOS:FILE)", CommandExec);
    ConsoleRegisterAlias("execute", "exec");
    ConsoleRegister("kill", "signal user task (PR-P4)", CommandKill);
}

void ShellCommandsSystemProcRegister(void) {
    ConsoleRegister2("list", "tasks", "list tasks", CommandPs);
    ConsoleRegisterAliasLine("ps", "list", "tasks");
    ConsoleRegisterAliasLine("tasks", "list", "tasks");

    ConsoleRegister2("run", "user", "run embedded hello ELF", CommandRunuser);
    ConsoleRegisterAliasLine("runuser", "run", "user");

    ConsoleRegister2("set", "priority", "set priority <pid> <prio>", CommandSetPriority);
    ConsoleRegisterAliasLine("nice", "set", "priority");

    ConsoleRegister("execute", "load ELF (TOYOS:FILE / A:FILE)", CommandExec);
    ConsoleRegisterAlias("execute", "exec");
    ConsoleRegister("kill", "signal user task (PR-P4)", CommandKill);
}
