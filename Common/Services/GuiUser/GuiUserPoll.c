/*
 * GuiUserPoll.c — GuiPollUserInput（关 / 按钮 / 键 / 客户区点）
 */
#include "GuiPrivate.h"
#include "Tasks.h"

int GuiPollUserInput(int Wid) {
    int I;
    int Id;
    int KeyEv;
    int Prefer;

    /* sleep 不让同核 Shell 转时，仍能把 HID 送进 USER 键队列 */
    TasksPumpKeyboard();
    Prefer = ResolveUserWindowIndex(Wid);
    /*
     * RaiseWindow 会搬槽位，用户态持有的 wid 可能过期。
     * 优先读解析后的 USER 槽；再扫整表兜底。
     */
    if (Prefer >= 0 && gWindows[Prefer].ClosePending) {
        gWindows[Prefer].ClosePending = 0;
        return 1;
    }
    for (I = 0; I < MAX_WINS; I++) {
        if (gWindows[I].Kind == GUI_WIN_USER && gWindows[I].ClosePending) {
            gWindows[I].ClosePending = 0;
            return 1;
        }
    }
    if (Prefer >= 0 && gWindows[Prefer].UserButtonClick >= 0 &&
        gWindows[Prefer].UserButtonClick < 4) {
        Id = gWindows[Prefer].UserButtonClick;
        gWindows[Prefer].UserButtonClick = -1;
        return 100 + Id;
    }
    for (I = 0; I < MAX_WINS; I++) {
        if (gWindows[I].Kind == GUI_WIN_USER &&
            gWindows[I].UserButtonClick >= 0 &&
            gWindows[I].UserButtonClick < 4) {
            Id = gWindows[I].UserButtonClick;
            gWindows[I].UserButtonClick = -1;
            return 100 + Id;
        }
    }
    KeyEv = GuiUserDequeueKeyEvent();
    if (KeyEv >= 0) {
        return KeyEv;
    }
    for (I = 0; I < MAX_WINS; I++) {
        if (gWindows[I].Active && gWindows[I].Kind == GUI_WIN_USER &&
            gWindows[I].UserClientClick) {
            UINT32 Cx;
            UINT32 Cy;

            Cx = gWindows[I].UserClickX;
            Cy = gWindows[I].UserClickY;
            gWindows[I].UserClientClick = 0;
            if (Cx > 1023u) {
                Cx = 1023u;
            }
            if (Cy > 1023u) {
                Cy = 1023u;
            }
            /* 400 + x + (y << 10)；不占用 0 / 1 / 100+id / 300+HID */
            return 400 + (int)Cx + ((int)Cy << 10);
        }
    }
    for (I = 0; I < MAX_WINS; I++) {
        if (gWindows[I].Active && gWindows[I].Kind == GUI_WIN_USER) {
            return 0;
        }
    }
    /*
     * 无活动 USER 窗：当作已关闭（1），勿回 -1。
     * 淡出竞态 / 进程已退时 ClosePending 可能已清，-1 会让 GUIDEMO 报 poll fail
     * 且残留窗槽导致第二次 exec 开不出窗。
     */
    return 1;
}
