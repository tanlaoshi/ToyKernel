/*
 * UiActions — 动作桩；装载见 ToyUiLayout.c
 * ID 须与 MyApp.uitxt → MyAppUi.h 一致（改 uitxt 后同步本文件）
 */
#include <ToyUi.h>
#include "MyAppUi.h"

void OnIdHello(int Wid) {
    (void)Wid;
}

void OnIdOk(int Wid) {
    ToyUiSetLabel(Wid, "OK");
}

void OnIdCancel(int Wid) {
    ToyUiSetLabel(Wid, "Cancel");
}

void OnIdChk(int Wid) {
    (void)Wid;
}

void OnIdGo(int Wid) {
    ToyUiSetLabel(Wid, "Go");
}

void UiDispatch(int Wid, int Ev) {
    if (Ev == TOY_UI_EVENT_CLOSE) {
        return;
    }
    if (Ev == TOY_UI_BUTTON_EVENT(ID_OK)) {
        OnIdOk(Wid);
    } else if (Ev == TOY_UI_BUTTON_EVENT(ID_CANCEL)) {
        OnIdCancel(Wid);
    } else if (Ev == TOY_UI_BUTTON_EVENT(ID_GO)) {
        OnIdGo(Wid);
    } else if (Ev == TOY_UI_CHECK_EVENT(ID_CHK)) {
        OnIdChk(Wid);
    } else if (Ev == TOY_UI_EVENT_CLICK) {
        OnIdHello(Wid);
    }
}
