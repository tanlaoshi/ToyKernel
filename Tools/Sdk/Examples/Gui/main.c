/*
 * Examples/Gui — libToyUi 标签 + 按钮（PR-A-examples）
 * 产物：MYGUI.ELF。无事件时 toy_yield()。
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
        printf("gui: create fail\n");
        return 1;
    }
    ToyUiSetLabel(Wid, "Hello GUI");
    ToyUiAddButton(Wid, 0, "OK");
    printf("gui: wid=%d\n", Wid);

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
