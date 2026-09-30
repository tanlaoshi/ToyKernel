/*
 * GuiClick.c — 按下命中编排（PR-F-guiclick-1）
 *
 * 关窗 / 桌面栏 / USER / 置顶分发 / 桌面落空；语义不变。
 * 核心：GuiPointer.c
 */
#include "GuiPrivate.h"

int GuiHandleClick(UINT32 X, UINT32 Y) {
    int i;

    /* 关闭钮可能被其它窗口挡住；先扫一遍所有窗口的 × 区域 */
    for (i = MAX_WINS - 1; i >= 0; i--) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            CloseWindow(i);
            return 1;
        }
    }

    if (GuiClickTryDesktopBar(X, Y)) {
        return 1;
    }
    if (GuiClickTryUserClient(X, Y)) {
        return 1;
    }
    if (GuiClickTryRaiseWindow(X, Y)) {
        return 1;
    }
    /* 未点中窗口：桌面图标（双击打开） / 开始菜单 */
    return GuiClickTryDesktopMiss(X, Y);
}
