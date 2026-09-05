/*
 * pkg/gui — 第一个 GUI 程序模板（PR-L3）
 * 构建：make -C User/pkg/gui
 * 文档：Documents/用户态GUI入门.md
 */
#include <stdio.h>
#include <unistd.h>
#include <ToyUi.h>
#include <toyos/syscall.h>

int main(void) {
    int Wid;
    int Ev;

    Wid = ToyUiCreateWindow("MyGui", 400, 240);
    if (Wid < 0) {
        printf("mygui: create fail\n");
        return 1;
    }
    ToyUiSetLabel(Wid, "Hello GUI");
    ToyUiAddButton(Wid, 0, "OK");
    printf("mygui: wid=%d\n", Wid);

    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            ToyUiSetLabel(Wid, "OK!");
        } else if (Ev == TOY_UI_EVENT_NONE) {
            toy_yield();
        }
    }
    return 0;
}
