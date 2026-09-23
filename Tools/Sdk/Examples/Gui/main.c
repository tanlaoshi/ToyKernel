/*
 * Examples/Gui — 标签/按钮 + 复选框/列表/输入框（PR-A-ui-api）
 * 产物：MYGUI.ELF。点客户区控件；底栏 OK 仍可用。
 */
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <ToyUi.h>
#include <toyos/syscall.h>

int main(void) {
    int Wid;
    int Ev;
    char Buf[TOY_UI_TEXT_MAX];

    Wid = ToyUiCreateWindow("MyGui", 420, 280);
    if (Wid < 0) {
        printf("Gui: create fail\n");
        return 1;
    }
    ToyUiSetLabel(Wid, "click box / list / field");
    ToyUiAddCheckBox(Wid, 0, 16, 28);
    ToyUiAddList(Wid, 0, 16, 52, 160);
    ToyUiListAddItem(Wid, 0, "Red");
    ToyUiListAddItem(Wid, 0, "Green");
    ToyUiListAddItem(Wid, 0, "Blue");
    ToyUiAddTextField(Wid, 0, 16, 116, 160);
    ToyUiAddButton(Wid, 0, "OK");
    printf("Gui: wid=%d (ToyUi %s)\n", Wid, TOY_UI_ABI_VERSION_STRING);

    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE) {
            break;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            ToyUiSetLabel(Wid, "OK!");
        } else if (Ev == TOY_UI_CHECK_EVENT(0)) {
            ToyUiSetLabel(Wid, ToyUiGetCheckBox(Wid, 0) ? "check on" : "check off");
        } else if (Ev == TOY_UI_LIST_EVENT(0)) {
            ToyUiGetTextField(Wid, 0, Buf, sizeof(Buf));
            ToyUiSetLabel(Wid, Buf);
        } else if (Ev == TOY_UI_TEXT_EVENT(0)) {
            ToyUiSetLabel(Wid, "field focus");
        } else if (Ev == TOY_UI_EVENT_NONE || Ev == TOY_UI_EVENT_CLICK) {
            sched_yield();
        }
    }
    return 0;
}
