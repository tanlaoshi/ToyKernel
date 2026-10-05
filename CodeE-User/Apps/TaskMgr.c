/*
 * 人话：这里是「任务管理器」窗口程序。列出内核里正在跑的任务。
 *       打开：开始菜单 → Apps → Task Manager（也可桌面图标）。
 *       或商店里安装 taskmgr 后再从 Apps 进。
 *
 * 从哪读：main（文件末尾）→ Refresh 拉一张快照 → Paint 画当前这一行。
 *
 * 别改：toy_task_snap / TOY_TASK_SNAP 的字段含义（与 Shell 命令 ps 同一份）。
 *       按钮编号 0/1/2 与 ToyUiAddButton 的添加顺序绑死。
 *       Pid 是槽位+1，和 kill 命令用的号码一样，不是 getpid() 的裸任务 Id。
 *
 * 想照着做：Documents/开发/如何写一个任务管理器App.md
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <ToyUi.h>
#include <toyos/task.h>

static TOY_TASK_SNAP gSnap;
static int gIndex;
static int gWid = -1;

static const char *StateWord(uint32_t Flags) {
    if (Flags & TOY_TASK_F_ZOMBIE) {
        return "zombie";
    }
    if (Flags & TOY_TASK_F_BLOCKED) {
        return "blocked";
    }
    if (Flags & TOY_TASK_F_CURRENT) {
        return "run*";
    }
    return "ready";
}

static void Paint(void) {
    char Line[120];
    const TOY_TASK_ENTRY *E;
    unsigned long Ms;

    if (gWid < 0) {
        return;
    }
    Ms = clock_ms();
    if (gSnap.Count == 0) {
        snprintf(Line, sizeof(Line), "no tasks\nfree=%upg t=%lums",
                 (unsigned)gSnap.FreePages, Ms);
        ToyUiSetLabel(gWid, Line);
        return;
    }
    if (gIndex < 0) {
        gIndex = 0;
    }
    if ((unsigned)gIndex >= gSnap.Count) {
        gIndex = 0;
    }
    E = &gSnap.Tasks[gIndex];
    /* List 控件只画色块、不写字，所以用标签两行显示当前任务。
     * 内核客户区认 '\\n' 换行；一行太长会被裁掉。 */
    snprintf(Line, sizeof(Line),
             "%d/%u pid=%d %s %s %s\nt=%u cpu=%d prio=%d free=%u t=%lums",
             gIndex + 1, (unsigned)gSnap.Count, (int)E->Pid, E->Name,
             (E->Flags & TOY_TASK_F_USER) ? "user" : "kern", StateWord(E->Flags),
             (unsigned)E->Ticks, (int)E->OnCpu, (int)E->Priority,
             (unsigned)gSnap.FreePages, Ms);
    ToyUiSetLabel(gWid, Line);
}

static int Refresh(void) {
    /* 与 Shell `ps` 同一份快照；Magic/Version 对不上说明内核 ABI 对不上。 */
    if (toy_task_snap(&gSnap) != 0) {
        return -1;
    }
    if (gSnap.Magic != TOY_TASK_SNAP_MAGIC || gSnap.Version != TOY_TASK_SNAP_VER) {
        return -1;
    }
    if (gSnap.Count == 0) {
        gIndex = 0;
    } else if ((unsigned)gIndex >= gSnap.Count) {
        gIndex = (int)gSnap.Count - 1;
    }
    Paint();
    return 0;
}

static void KillCurrent(void) {
    const TOY_TASK_ENTRY *E;

    if (gSnap.Count == 0 || gIndex < 0 || (unsigned)gIndex >= gSnap.Count) {
        ToyUiSetLabel(gWid, "nothing to kill");
        return;
    }
    /* 只杀用户态任务；内核任务和自己都不能杀。Pid 见文件头。 */
    E = &gSnap.Tasks[gIndex];
    if (!(E->Flags & TOY_TASK_F_USER)) {
        ToyUiSetLabel(gWid, "refuse: kernel task");
        return;
    }
    if (E->Pid == gSnap.SelfPid) {
        ToyUiSetLabel(gWid, "refuse: self");
        return;
    }
    if (kill((pid_t)E->Pid, SIGKILL) != 0) {
        ToyUiSetLabel(gWid, "kill failed");
        return;
    }
    Refresh();
}

static void DrainClicks(void) {
    int Ev;

    /* AddButton 时可能已经排队点击，先抽空，免得一开窗就误触发 Refresh/Next/Kill。 */

    for (;;) {
        Ev = ToyUiPoll(gWid);
        if (Ev <= 0) {
            break;
        }
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
    }
}

int main(void) {
    int Ev;

    gWid = ToyUiCreateWindow("TaskMgr", 520, 200);
    if (gWid < 0) {
        printf("taskmgr: create fail\n");
        return 1;
    }
    /* 0=Refresh 1=Next 2=Kill；ToyUi 按添加顺序编号，对调这里会点错按钮。 */
    if (ToyUiAddButton(gWid, 0, "Refresh") != 0 ||
        ToyUiAddButton(gWid, 1, "Next") != 0 ||
        ToyUiAddButton(gWid, 2, "Kill") != 0) {
        printf("taskmgr: button fail\n");
        return 1;
    }
    DrainClicks();
    gIndex = 0;
    if (Refresh() != 0) {
        printf("taskmgr: snap fail\n");
        ToyUiSetLabel(gWid, "snap syscall failed");
    }
    printf("taskmgr: wid=%d self=%d count=%u\n", gWid, (int)gSnap.SelfPid,
           (unsigned)gSnap.Count);

    for (;;) {
        Ev = ToyUiPoll(gWid);
        if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            if (Refresh() != 0) {
                ToyUiSetLabel(gWid, "refresh fail");
            }
        } else if (Ev == TOY_UI_BUTTON_EVENT(1)) {
            if (gSnap.Count > 0) {
                gIndex = (gIndex + 1) % (int)gSnap.Count;
                Paint();
            }
        } else if (Ev == TOY_UI_BUTTON_EVENT(2)) {
            KillCurrent();
        } else {
            sched_yield();
        }
    }
    printf("taskmgr: closed\n");
    return 0;
}
