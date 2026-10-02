/*
 * SyscallCwd.c — getcwd / chdir，以及相对路径拼到任务 Cwd
 */
#include "SyscallPrivate.h"
#include "Scheduler.h"
#include "Fat.h"
#include "VirtualMemory.h"

int CwdResolve(TASK *T, char *Path, int Max) {
    char Tmp[PATH_MAX_LEN + 1];
    int n;
    int i;
    int Vol;

    if (!T || !Path || Max <= 1) {
        return -1;
    }
    if (Path[0] == 0 || Path[0] == '/' || !T->Cwd[0]) {
        return 0;
    }
    Vol = 0;
    for (i = 0; Path[i] && Path[i] != '/' && i < 16; i++) {
        if (Path[i] == ':') {
            Vol = 1;
        }
    }
    if (Vol) {
        return 0;
    }
    n = 0;
    for (i = 0; T->Cwd[i] && n < Max - 1; i++) {
        Tmp[n++] = T->Cwd[i];
    }
    if (n > 0 && Tmp[n - 1] != '/') {
        Tmp[n++] = '/';
    }
    for (i = 0; Path[i] == '/'; i++) {
    }
    for (; Path[i] && n < Max - 1; i++) {
        Tmp[n++] = Path[i];
    }
    if (Path[i]) {
        return -1;
    }
    Tmp[n] = 0;
    for (i = 0; i <= n; i++) {
        Path[i] = Tmp[i];
    }
    return 0;
}

int SysGetcwd(UINT64 UserBuf, UINTN Len) {
    char Out[PATH_MAX_LEN + 2];
    int n;
    int i;
    TASK *T = SchedulerCurrent();

    if (!T || !T->IsUser || UserBuf == 0 || Len < 2) {
        return -1;
    }
    n = 0;
    if (!T->Cwd[0]) {
        Out[n++] = '/';
    } else if (T->Cwd[0] != '/') {
        Out[n++] = '/';
        for (i = 0; T->Cwd[i] && n < (int)sizeof(Out) - 1; i++) {
            Out[n++] = T->Cwd[i];
        }
    } else {
        for (i = 0; T->Cwd[i] && n < (int)sizeof(Out) - 1; i++) {
            Out[n++] = T->Cwd[i];
        }
    }
    Out[n] = 0;
    if ((UINTN)n + 1 > Len) {
        return -1;
    }
    if (VirtualMemoryCopyToUser(UserBuf, Out, (UINTN)n + 1) < 0) {
        return -1;
    }
    return 0;
}

int SysChdir(UINT64 UserPath) {
    char Path[PATH_MAX_LEN + 1];
    FAT_FILE_STAT St;
    TASK *T = SchedulerCurrent();
    const char *S;
    int n;

    if (!T || !T->IsUser) {
        return -1;
    }
    if (CopyUserCString(Path, UserPath, PATH_MAX_LEN) != 0 || Path[0] == 0) {
        return -1;
    }
    if (Path[0] == '.' && Path[1] == 0) {
        return 0;
    }
    if (CwdResolve(T, Path, (int)sizeof(Path)) != 0) {
        return -1;
    }
    if (Path[0] == 0 || (Path[0] == '/' && Path[1] == 0)) {
        T->Cwd[0] = 0;
        return 0;
    }
    if (SchedulerFdFileStat(T, Path, &St) < 0) {
        return -1;
    }
    if ((St.Attr & FAT_ATTR_DIR) == 0) {
        return -1;
    }
    S = Path;
    while (*S == '/') {
        S++;
    }
    n = 0;
    while (*S && n < (int)sizeof(T->Cwd) - 1) {
        T->Cwd[n++] = *S++;
    }
    if (*S) {
        return -1;
    }
    T->Cwd[n] = 0;
    return 0;
}
