/*
 * Style — ToyUiStyle 范例（PR-UI-style-0）
 * 切换样式表，复选框颜色跟着变。
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyUiStyle.h>

static int On;
static int UseDark;

static void Draw(int Wid) {
    ToyUiSetLabel(Wid, UseDark ? "dark style" : "light style");
    ToyUiAddButton(Wid, 0, "Toggle");
    ToyUiAddButton(Wid, 1, "Repaint");
    ToyUiAddCheckBox(Wid, 0, 16, 28);
    ToyUiSetCheckBox(Wid, 0, On);
}

int main(void) {
    int Wid;
    int Ev;

    Wid = ToyUiCreateWindow("Style", 420, 280);
    if (Wid < 0) {
        printf("style: create fail\n");
        return 1;
    }
    Draw(Wid);
    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            UseDark = !UseDark;
            if (UseDark) {
                TOY_UI_STYLE S = ToyUiStyleDefault();
                S.CheckFillOn = 0x0000D0FFu;
                S.CheckFillOff = 0x00202020u;
                S.CheckBorder = 0x00FFFFFFu;
                ToyUiSetStyle(&S);
            } else {
                ToyUiSetStyle(0);
            }
            Draw(Wid);
        } else if (Ev == TOY_UI_BUTTON_EVENT(1)) {
            Draw(Wid);
        } else if (Ev == TOY_UI_CHECK_EVENT(0)) {
            On = ToyUiGetCheckBox(Wid, 0);
        }
        sched_yield();
    }
    return 0;
}
