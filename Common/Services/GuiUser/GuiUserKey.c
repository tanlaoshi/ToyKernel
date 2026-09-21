/*
 * GuiUserKey.c — 用户窗键盘入队（焦点路由）
 */
#include "GuiPrivate.h"

void GuiUserEnqueueKey(UINT8 HidKey) {
    int Idx;

    if (HidKey == 0 || HidKey > GUI_USER_KEY_HID_MAX) {
        return;
    }
    Idx = gFocusWin;
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active ||
        gWindows[Idx].Kind != GUI_WIN_USER) {
        return;
    }
    if (gWindows[Idx].UserKeyCount >= GUI_USER_KEY_Q) {
        /* 满则丢最旧，保最新（游戏/连打更要新键） */
        {
            int I;
            for (I = 1; I < GUI_USER_KEY_Q; I++) {
                gWindows[Idx].UserKeyQ[I - 1] = gWindows[Idx].UserKeyQ[I];
            }
            gWindows[Idx].UserKeyCount = GUI_USER_KEY_Q - 1;
        }
    }
    gWindows[Idx].UserKeyQ[gWindows[Idx].UserKeyCount++] = HidKey;
}

int GuiUserDequeueKeyEvent(void) {
    int I;
    UINT8 Hid;

    for (I = 0; I < MAX_WINS; I++) {
        if (!gWindows[I].Active || gWindows[I].Kind != GUI_WIN_USER) {
            continue;
        }
        if (gWindows[I].UserKeyCount == 0) {
            continue;
        }
        Hid = gWindows[I].UserKeyQ[0];
        {
            int J;
            for (J = 1; J < gWindows[I].UserKeyCount; J++) {
                gWindows[I].UserKeyQ[J - 1] = gWindows[I].UserKeyQ[J];
            }
        }
        gWindows[I].UserKeyCount--;
        return GUI_USER_KEY_BASE + (int)Hid;
    }
    return -1;
}
