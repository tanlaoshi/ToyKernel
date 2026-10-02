/*
 * DesktopClick.c — 图标点选（PR-S3-desktopclick-1）
 *
 * 任务栏 / 开始菜单见 DesktopTaskbarClick.c。核心：Desktop.c。
 */
#include "DesktopPrivate.h"

void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT || !gIcons[Idx].Present) {
        return;
    }
    DrawOneIconOccluded(&gIcons[Idx], Idx == gDeskSelected);
}

void SelectIcon(int Hit, UINT32 X, UINT32 Y, UINT64 Now) {
    int Prev = gDeskSelected;

    gDeskSelected = Hit;
    gSelectClock = Now;
    gSelectX = X;
    gSelectY = Y;
    if (Prev >= 0 && Prev != Hit) {
        RedrawIconIndex(Prev);
    }
    RedrawIconIndex(Hit);
}

int DesktopHandleClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction,
                       char *OutExecPath, UINTN ExecPathMax) {
    int i;
    int Hit;
    int Prev;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    if (OutAction) {
        *OutAction = DESKTOP_ACTION_NONE;
    }
    if (OutExecPath && ExecPathMax > 0) {
        OutExecPath[0] = 0;
    }

    if (HandleTaskbarClick(X, Y, OutAction, OutExecPath, ExecPathMax)) {
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        return 1;
    }

    Hit = -1;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (!gIcons[i].Present) {
            continue;
        }
        if (PointInIcon(&gIcons[i], X, Y)) {
            Hit = i;
            break;
        }
    }

    Now = DesktopClock();
    if (Hit < 0) {
        Prev = gDeskSelected;
        gDeskSelected = -1;
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        if (Prev >= 0) {
            RedrawIconIndex(Prev);
        }
        return 0;
    }

    Dt = (Now >= gSelectClock) ? (Now - gSelectClock) : DESKTOP_DBLCLICK_MAX + 1;
    Dx = (X >= gSelectX) ? (X - gSelectX) : (gSelectX - X);
    Dy = (Y >= gSelectY) ? (Y - gSelectY) : (gSelectY - Y);

    if (Hit == gDeskSelected &&
        Dt <= DESKTOP_DBLCLICK_MAX &&
        Dx <= DESKTOP_DBLCLICK_SLOP &&
        Dy <= DESKTOP_DBLCLICK_SLOP) {
        gMenuOpen = 0;
        gMenuAppsOpen = 0;
        gMenuGameOpen = 0;
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        if (OutAction) {
            *OutAction = gIcons[Hit].Action;
        }
        if (gIcons[Hit].Action == DESKTOP_ACTION_EXEC &&
            gIcons[Hit].ExecPath && OutExecPath && ExecPathMax > 0) {
            MenuCopyStr(OutExecPath, (int)ExecPathMax, gIcons[Hit].ExecPath);
        }
        gDeskSelected = -1;
        return 1;
    }

    SelectIcon(Hit, X, Y, Now);
    /* PR-G-desk-1：武装拖放；位移超阈值才真正移动 */
    gIconDragIdx = Hit;
    gIconDragOffX = (INT32)X - (INT32)gIcons[Hit].X;
    gIconDragOffY = (INT32)Y - (INT32)gIcons[Hit].Y;
    gIconDragStartX = X;
    gIconDragStartY = Y;
    gIconDragMoved = 0;
    return 1;
}
