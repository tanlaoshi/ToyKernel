/*
 * DesktopMenuCover.c — 开始菜单覆盖矩形（局部刷新擦除用）
 */
#include "DesktopPrivate.h"
#include "Desktop.h"

int DesktopMenuCoverGet(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (!X || !Y || !W || !H) {
        return 0;
    }
    if (gMenuCoverW == 0 || gMenuCoverH == 0) {
        *X = *Y = *W = *H = 0;
        return 0;
    }
    *X = gMenuCoverX;
    *Y = gMenuCoverY;
    *W = gMenuCoverW;
    *H = gMenuCoverH;
    return 1;
}

void DesktopMenuCoverClear(void) {
    gMenuCoverX = 0;
    gMenuCoverY = 0;
    gMenuCoverW = 0;
    gMenuCoverH = 0;
}

/* DrawStartMenuRaw 末：主面板 ∪ flyout 并集 */
void DesktopMenuCoverUpdate(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;

    MenuGeom(&Mx, &My, &Mw, &Mh);
    X0 = Mx;
    Y0 = My;
    X1 = Mx + Mw;
    Y1 = My + Mh;
    if (gMenuAppsOpen) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;

        AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
        if (Fx < X0) {
            X0 = Fx;
        }
        if (Fy < Y0) {
            Y0 = Fy;
        }
        if (Fx + Fw > X1) {
            X1 = Fx + Fw;
        }
        if (Fy + Fh > Y1) {
            Y1 = Fy + Fh;
        }
    }
    if (gMenuGameOpen) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;

        GameFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
        if (Fx < X0) {
            X0 = Fx;
        }
        if (Fy < Y0) {
            Y0 = Fy;
        }
        if (Fx + Fw > X1) {
            X1 = Fx + Fw;
        }
        if (Fy + Fh > Y1) {
            Y1 = Fy + Fh;
        }
    }
    gMenuCoverX = X0;
    gMenuCoverY = Y0;
    gMenuCoverW = (X1 > X0) ? (X1 - X0) : 0;
    gMenuCoverH = (Y1 > Y0) ? (Y1 - Y0) : 0;
}
