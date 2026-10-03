/*
 * GuiIm — ToyUiIm Label/Check/Button 范例（PR-UI-im-1，≈ GuiDemo）
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyUiIm.h>

int main(void) {
    int Wid;
    int On;
    const char *Msg;

    Wid = ToyUiCreateWindow("GuiIm", 480, 280);
    if (Wid < 0) {
        printf("guiim: create fail\n");
        return 1;
    }
    On = 0;
    Msg = "Click OK or check";
    for (;;) {
        if (ToyUiImBegin(Wid, 0) != 0) {
            break;
        }
        if (ToyUiImClosed()) {
            ToyUiImEnd();
            break;
        }
        if (ToyUiImCheck(&On)) {
            Msg = On ? "check on" : "check off";
        }
        if (ToyUiImButton("OK")) {
            Msg = "OK clicked";
        }
        if (ToyUiImButton("Cancel")) {
            Msg = "Cancel clicked";
        }
        ToyUiImLabel(Msg);
        ToyUiImEnd();
        sched_yield();
    }
    return 0;
}
