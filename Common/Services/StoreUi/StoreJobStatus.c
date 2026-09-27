/*
 * StoreJobStatus.c — Job 状态行文案（PR-S-job）
 */
#include "StoreUiPrivate.h"
#include "StoreJob.h"
#include "Fat.h"
#include "Console.h"

void StoreJobStatusWithId(const char *Verb, const char *Id) {
    char Buf[80];
    int i = 0;
    int j;

    if (!Verb) {
        Verb = "?";
    }
    while (Verb[i] && i < 20) {
        Buf[i] = Verb[i];
        i++;
    }
    if (Id && Id[0] && i < 76) {
        Buf[i++] = ':';
        Buf[i++] = ' ';
        for (j = 0; Id[j] && i < 78; j++) {
            Buf[i++] = Id[j];
        }
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
}

void StoreJobStatusProgress(const char *Verb, const char *Id, int Cur, int Total) {
    char Buf[80];
    int i = 0;
    int j;
    int n;

    if (!Verb) {
        Verb = "?";
    }
    while (Verb[i] && i < 16) {
        Buf[i] = Verb[i];
        i++;
    }
    if (Id && Id[0] && i < 60) {
        Buf[i++] = ':';
        Buf[i++] = ' ';
        for (j = 0; Id[j] && i < 60; j++) {
            Buf[i++] = Id[j];
        }
    }
    if (Total > 0 && i < 70) {
        Buf[i++] = ' ';
        Buf[i++] = '(';
        n = Cur;
        if (n >= 10 && i < 76) {
            Buf[i++] = (char)('0' + (n / 10) % 10);
        }
        if (i < 76) {
            Buf[i++] = (char)('0' + n % 10);
        }
        Buf[i++] = '/';
        n = Total;
        if (n >= 10 && i < 76) {
            Buf[i++] = (char)('0' + (n / 10) % 10);
        }
        if (i < 76) {
            Buf[i++] = (char)('0' + n % 10);
        }
        Buf[i++] = ')';
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
}

void StoreJobStatusCopy(const char *Id, UINTN Got, UINTN Size) {
    char Buf[80];
    int n = 0;
    int k;
    const char *P = Id ? Id : "";

    Buf[n++] = 'c';
    Buf[n++] = 'o';
    Buf[n++] = 'p';
    Buf[n++] = 'y';
    Buf[n++] = ':';
    Buf[n++] = ' ';
    while (*P && n < 40) {
        Buf[n++] = *P++;
    }
    if (Size > 0 && n < 70) {
        Buf[n++] = ' ';
        k = (int)(Got / 1024u);
        if (k >= 100 && n < 76) {
            Buf[n++] = (char)('0' + (k / 100) % 10);
        }
        if (k >= 10 && n < 76) {
            Buf[n++] = (char)('0' + (k / 10) % 10);
        }
        if (n < 76) {
            Buf[n++] = (char)('0' + k % 10);
        }
        Buf[n++] = 'K';
    }
    Buf[n] = 0;
    StoreSetStatus(Buf);
}

void StoreJobBusyRepaint(void) {
    if (StoreUiIsFocused()) {
        StoreUiRepaint();
    }
}

void StoreJobFinishStatus(STORE_JOB_KIND Kind, int Err, int PlanN) {
    /* Worker 异步结束：完成行回发起 Shell；无 Hold 时 WriteToJobShell≈串口+焦点 */
    if (!ConsolePromptSuspended()) {
        ConsoleWriteToJobShell("\n");
    }
    if (Err == STORE_JOB_ERR_CANCEL) {
        StoreSetStatus("cancelled");
        ConsoleWriteToJobShell("store job: cancelled\n");
        if (!ConsolePromptSuspended() && !StoreJobShellPumping()) {
            ConsoleJobReleasePrompt();
        }
        return;
    }
    if (Err == FAT_OK && Kind == STORE_JOB_INSTALL && PlanN == 0) {
        StoreSetStatus("already installed");
        ConsoleWriteToJobShell("store job: already installed\n");
        if (!ConsolePromptSuspended() && !StoreJobShellPumping()) {
            ConsoleJobReleasePrompt();
        }
        return;
    }
    if (Err == FAT_OK) {
        if (Kind == STORE_JOB_INSTALL) {
            StoreSetStatus("installed");
            ConsoleWriteToJobShell("store job: installed\n");
        } else if (Kind == STORE_JOB_REMOVE) {
            StoreSetStatus("removed");
            ConsoleWriteToJobShell("store job: removed\n");
        } else if (Kind == STORE_JOB_FETCH) {
            StoreSetStatus("fetched");
            ConsoleWriteToJobShell("store job: fetched\n");
            ConsoleWriteToJobShell("hint: store install <id>\n");
        } else {
            StoreSetStatus("sync ok");
            ConsoleWriteToJobShell("store job: sync ok\n");
        }
    } else if (Kind == STORE_JOB_INSTALL) {
        StoreSetStatus("install fail");
        ConsoleWriteToJobShell("store job: install fail\n");
    } else if (Kind == STORE_JOB_REMOVE) {
        StoreSetStatus("remove fail");
        ConsoleWriteToJobShell("store job: remove fail\n");
        if (Err == FAT_ERR_INVAL) {
            ConsoleWriteToJobShell("hint: still required by dependents; remove app first\n");
        }
    } else if (Kind == STORE_JOB_FETCH) {
        StoreSetStatus("fetch fail");
        ConsoleWriteToJobShell("store job: fetch fail\n");
        if (Err == -41 || Err == -2) {
            ConsoleWriteToJobShell("hint: HTTP not 200\n");
        } else if (Err == -42 || Err == -3) {
            ConsoleWriteToJobShell("hint: hash mismatch\n");
        } else if (Err == -40) {
            ConsoleWriteToJobShell("hint: net/tcp fail\n");
        } else if (Err == -43) {
            ConsoleWriteToJobShell("hint: out of memory\n");
        }
    } else {
        StoreSetStatus("sync fail (need repo)");
        ConsoleWriteToJobShell("store job: sync fail\n");
    }
    if (!ConsolePromptSuspended() && !StoreJobShellPumping()) {
        ConsoleJobReleasePrompt();
    }
}
