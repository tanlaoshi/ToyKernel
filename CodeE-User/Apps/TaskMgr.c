/*
 * TaskMgr.c — 任务管理器（课堂范例 GUI App）
 *
 * 读 SYS_TASK_SNAP（与 Shell `ps` 同源）。
 * 注意：libToyUi 的 List 只画色块、不绘文字 → 本 App 用 SetLabel 显示当前行，
 * Next 翻任务；Refresh / Kill 仍可用。
 * Shell：exec TASKMGR.ELF
 * 流程文档：Documents/开发/如何写一个任务管理器App.md
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
    /* 两行：避免单行超出客户区宽度被裁掉（内核 ClientText 认 '\\n'） */
    snprintf(Line, sizeof(Line),
             "%d/%u pid=%d %s %s %s\nt=%u cpu=%d prio=%d free=%u t=%lums",
             gIndex + 1, (unsigned)gSnap.Count, (int)E->Pid, E->Name,
             (E->Flags & TOY_TASK_F_USER) ? "user" : "kern", StateWord(E->Flags),
             (unsigned)E->Ticks, (int)E->OnCpu, (int)E->Priority,
             (unsigned)gSnap.FreePages, Ms);
    ToyUiSetLabel(gWid, Line);
}

static int Refresh(void) {
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
