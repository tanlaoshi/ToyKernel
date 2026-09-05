/*
 * GuiDemo.c — PR-G15：链 libToyUi/libToyGfx，不改内核菜单即可画按钮
 * Shell：exec GUIDEMO.ELF
 */
#include "stdio.h"
#include "unistd.h"
#include "ToyUi.h"
#include "toy_syscall.h"

int main(void) {
    int WindowId;
    int Event;

    WindowId = ToyUiCreateWindow("GuiDemo", 480, 280);
    if (WindowId < 0) {
        printf("guidemo: create fail\n");
        return 1;
    }
    if (ToyUiSetLabel(WindowId, "Hello from libToyUi") != 0) {
        printf("guidemo: label fail\n");
        return 1;
    }
    if (ToyUiAddButton(WindowId, 0, "OK") != 0 ||
        ToyUiAddButton(WindowId, 1, "Cancel") != 0) {
        printf("guidemo: button fail\n");
        return 1;
    }
    /* 日志只走串口：焦点在 USER 时 printf 不再画进本窗（见 ConsoleWrite） */
    printf("guidemo: wid=%d\n", WindowId);
    for (;;) {
        Event = ToyUiPoll(WindowId);
        if (Event < 0) {
            printf("guidemo: poll fail\n");
            return 1;
        }
        if (Event == TOY_UI_EVENT_CLOSE) {
            break;
        }
        if (Event == TOY_UI_BUTTON_EVENT(0)) {
            ToyUiSetLabel(WindowId, "OK clicked");
        } else if (Event == TOY_UI_BUTTON_EVENT(1)) {
            ToyUiSetLabel(WindowId, "Cancel clicked");
        } else {
            toy_yield();
        }
    }
    printf("guidemo: closed ok\n");
    return 0;
}
