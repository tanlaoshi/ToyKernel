/*
 * DesktopIconDrag.c — 桌面图标拖放
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"
#include "HalVideo.h"

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
    int Idx;
    int Moved;

    Idx = gIconDragIdx;
    Moved = gIconDragMoved;
    /* 先清拖态，避免 SaveIconLayout 阻塞时仍算「拖动中」 */
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    gIconDragOffX = 0;
    gIconDragOffY = 0;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    if (!Moved) {
        return;
    }
    {
        UINT32 Ox;
        UINT32 Oy;
        UINT32 Ow;
        UINT32 Oh;

        /*
         * 必须先 Snap 再擦落点：ClearIconFootprint → DesktopDrawRect 会按
         * gIcons 重画；若坐标仍在松手处，刚擦掉的残影会被立刻画回。
         * 视觉归位优先于 SaveIconLayout（DB/MSC 慢时勿卡住不吸附）。
         */
        IconBounds(&gIcons[Idx], &Ox, &Oy, &Ow, &Oh);
        SnapIconToGrid(&gIcons[Idx].X, &gIcons[Idx].Y);
        /* 落点与吸附格都擦干净（含字灰边） */
        {
            UINT32 Sw = 0;
            UINT32 Sh = 0;
            UINT32 Nx;
            UINT32 Ny;
            UINT32 Nw;
            UINT32 Nh;
            int Saved = gIcons[Idx].Present;

            HalVideoGetSize(&Sw, &Sh);
            if (Ox >= 4u) {
                Ox -= 4u;
                Ow += 4u;
            }
            if (Oy >= 4u) {
                Oy -= 4u;
                Oh += 4u;
            }
            Ow += 4u;
            Oh += 4u;
            if (Sw && Ox + Ow > Sw) {
                Ow = Sw - Ox;
            }
            if (Sh && Oy + Oh > Sh) {
                Oh = Sh - Oy;
            }
            IconBounds(&gIcons[Idx], &Nx, &Ny, &Nw, &Nh);
            if (Nx >= 4u) {
                Nx -= 4u;
                Nw += 4u;
            }
            if (Ny >= 4u) {
                Ny -= 4u;
                Nh += 4u;
            }
            Nw += 4u;
            Nh += 4u;
            if (Sw && Nx + Nw > Sw) {
                Nw = Sw - Nx;
            }
            if (Sh && Ny + Nh > Sh) {
                Nh = Sh - Ny;
            }
            gIcons[Idx].Present = 0;
            ClearIconFootprint(Ox, Oy, Ow, Oh);
            ClearIconFootprint(Nx, Ny, Nw, Nh);
            gIcons[Idx].Present = Saved;
        }
        DrawOneIconOccluded(&gIcons[Idx], Idx == gDeskSelected);
        HalVideoPresentFlush();
        SaveIconLayout();
    }
}
