/*
 * DesktopIconMove.c — 图标拖移擦旧画新（PR-S3-deskiconlayout-1）
 *
 * 从 DesktopIconLayout.c 原样搬家；不改语义。
 */
#include "DesktopPrivate.h"
#include "HalVideo.h"

static void IconFootprintPad(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H,
                             UINT32 Pad, UINT32 Sw, UINT32 Sh) {
    if (!X || !Y || !W || !H || Pad == 0) {
        return;
    }
    if (*X >= Pad) {
        *X -= Pad;
        *W += Pad;
    } else {
        *W += *X;
        *X = 0;
    }
    if (*Y >= Pad) {
        *Y -= Pad;
        *H += Pad;
    } else {
        *H += *Y;
        *Y = 0;
    }
    *W += Pad;
    *H += Pad;
    if (Sw != 0 && *X + *W > Sw) {
        *W = Sw - *X;
    }
    if (Sh != 0 && *Y + *H > Sh) {
        *H = Sh - *Y;
    }
}

void MoveIconTo(int Idx, UINT32 NewX, UINT32 NewY) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;
    UINT32 Nx;
    UINT32 Ny;
    UINT32 Nw;
    UINT32 Nh;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 Pad = 4u;
    int SavedPresent;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    ClampIconPos(&NewX, &NewY);
    if (gIcons[Idx].X == NewX && gIcons[Idx].Y == NewY) {
        return;
    }
    HalVideoGetSize(&Sw, &Sh);
    IconBounds(&gIcons[Idx], &Ox, &Oy, &Ow, &Oh);
    IconFootprintPad(&Ox, &Oy, &Ow, &Oh, Pad, Sw, Sh);

    gIcons[Idx].X = NewX;
    gIcons[Idx].Y = NewY;
    IconBounds(&gIcons[Idx], &Nx, &Ny, &Nw, &Nh);
    IconFootprintPad(&Nx, &Ny, &Nw, &Nh, Pad, Sw, Sh);

    /*
     * 擦旧∪新两块（非对角 AABB，免误伤路径上其它图标）。
     * Clear 时暂藏本图标，避免 DesktopDrawRect 按新坐标把半个图标画回旧脚印。
     */
    SavedPresent = gIcons[Idx].Present;
    gIcons[Idx].Present = 0;
    ClearIconFootprint(Ox, Oy, Ow, Oh);
    ClearIconFootprint(Nx, Ny, Nw, Nh);
    gIcons[Idx].Present = SavedPresent;
    DrawOneIconRaw(&gIcons[Idx], Idx == gDeskSelected);
}
