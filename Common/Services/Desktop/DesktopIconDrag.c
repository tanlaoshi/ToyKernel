/*
 * DesktopIconDrag.c — 桌面图标拖放
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

int DesktopIconDragActive(void) {
    return gIconDragIdx >= 0;
}

void DesktopIconDragUpdate(UINT32 X, UINT32 Y) {
    UINT32 Dx;
    UINT32 Dy;
    INT32 Nx;
    INT32 Ny;
    UINT32 Ux;
    UINT32 Uy;

    if (gIconDragIdx < 0 || gIconDragIdx >= DESKTOP_ICON_COUNT) {
        return;
    }
    Dx = (X >= gIconDragStartX) ? (X - gIconDragStartX) : (gIconDragStartX - X);
    Dy = (Y >= gIconDragStartY) ? (Y - gIconDragStartY) : (gIconDragStartY - Y);
    if (!gIconDragMoved) {
        if (Dx <= DESKTOP_DRAG_THRESH && Dy <= DESKTOP_DRAG_THRESH) {
            return;
        }
        gIconDragMoved = 1;
        /* 已进入拖放：清双击时钟，避免松手后再点误开 */
        gSelectClock = 0;
    }
    Nx = (INT32)X - gIconDragOffX;
    Ny = (INT32)Y - gIconDragOffY;
    if (Nx < 0) {
        Nx = 0;
    }
    if (Ny < 0) {
        Ny = 0;
    }
    Ux = (UINT32)Nx;
    Uy = (UINT32)Ny;
    MoveIconTo(gIconDragIdx, Ux, Uy);
}

void DesktopIconDragEnd(void) {
    if (gIconDragIdx < 0) {
        return;
    }
    if (gIconDragMoved) {
        UINT32 X;
        UINT32 Y;
        UINT32 W;
        UINT32 H;

        ClampIconPos(&gIcons[gIconDragIdx].X, &gIcons[gIconDragIdx].Y);
        SaveIconLayout();
        /* 置顶残影 → 擦脚印还原窗/影，再按避让重画落位图标 */
        IconBounds(&gIcons[gIconDragIdx], &X, &Y, &W, &H);
        ClearIconFootprint(X, Y, W, H);
        DrawOneIconOccluded(&gIcons[gIconDragIdx],
                            gIconDragIdx == gDeskSelected);
        HalVideoPresent();
    }
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    gIconDragOffX = 0;
    gIconDragOffY = 0;
}
