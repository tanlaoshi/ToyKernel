/*
 * windemo.c — PR-G14：create_window / damage / poll_input 用户窗演示
 * Shell：exec WINDEMO.ELF → 弹窗显示字符串 → 点 × 退出
 */
#include "stdio.h"
#include "unistd.h"
#include "toy_syscall.h"

int main(void) {
    int Wid;
    int Ev;

    Wid = create_window("WinDemo", 420, 240);
    if (Wid < 0) {
        printf("windemo: create_window fail\n");
        return 1;
    }
    if (damage(Wid, "Hello from user window!") != 0) {
        printf("windemo: damage fail\n");
        return 1;
    }
    printf("windemo: wid=%d — close the window to exit\n", Wid);
    for (;;) {
        Ev = poll_input(Wid);
        if (Ev < 0) {
            printf("windemo: poll fail\n");
            return 1;
        }
        if (Ev == 1) {
            break;
        }
        toy_yield();
    }
    printf("windemo: closed ok\n");
    return 0;
}
