/* DesktopNetTrayPop.c — 网态托盘弹层 / 点击（BOX-3） */
#include "DesktopPrivate.h"
#include "HalDevices.h"
#include "NetConfig.h"

#define NET_ICON      16u
#define NET_POP_PAD   8u
#define NET_POP_W     200u
#define NET_POP_LINES 5

static void ClockAnchor(UINT32 *ClockXOut) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 ClockW;
    char Clock[8];

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)BarY;
    Clock[0] = '0';
    Clock[1] = '0';
    Clock[2] = ':';
    Clock[3] = '0';
    Clock[4] = '0';
    Clock[5] = 0;
    ClockW = FontStringWidth(Clock);
    *ClockXOut = (Sw > ClockW + 12u) ? (Sw - ClockW - 12u) : 0;
}

static void PopupGeom(UINT32 *Px, UINT32 *Py, UINT32 *Pw, UINT32 *Ph) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 H;

    TaskbarGeom(&BarY, &Sw, &Sh);
    H = NET_POP_PAD * 2u + (UINT32)NET_POP_LINES * FontCellH();
    *Pw = NET_POP_W;
    *Ph = H;
    *Px = (Sw > NET_POP_W + 8u) ? (Sw - NET_POP_W - 8u) : 0;
    *Py = (BarY > H + 4u) ? (BarY - H - 4u) : 0;
}

void DesktopNetTrayDrawPopup(void) {
    UINT32 Px, Py, Pw, Ph, Ty;
    char Line[40];
    int Up = 0;
    UINT32 Mbps = 0;
    int Fd = 0;
    UINT32 Ip;
    NET_TRAY_KIND Kind;

    if (!DesktopNetTrayIsOpen()) {
        return;
    }
    PopupGeom(&Px, &Py, &Pw, &Ph);
    UiFillRectangle(Px, Py, Pw, Ph, ThemeControlFace());
    UiDrawRectangle(Px, Py, Pw, Ph, ThemeMenuBorder());

    Ty = Py + NET_POP_PAD;
    Kind = DesktopNetTrayDetectKind();
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty,
        Kind == NET_TRAY_WIFI ? "Wi-Fi" :
        (Kind == NET_TRAY_WIRED ? "Wired" : "No network"), ThemeText());
    Ty += FontCellH();

    Ip = DesktopNetTrayCurrentIp();
    DesktopNetTrayMakePrefixed(Line, (int)sizeof(Line), "ip  ", Ip);
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, ThemeTextMuted());
    Ty += FontCellH();

    DesktopNetTrayMakePrefixed(Line, (int)sizeof(Line), "gw  ", NetConfigGetGw());
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, ThemeTextMuted());
    Ty += FontCellH();

    DesktopNetTrayMakePrefixed(Line, (int)sizeof(Line), "dns ", NetConfigGetDns());
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, ThemeTextMuted());
    Ty += FontCellH();

    if (!HalNetReady()) {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, "link n/a", ThemeTextMuted());
    } else if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty,
                             Up ? "link up" : "link down", ThemeTextMuted());
    } else {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, "link n/a", ThemeTextMuted());
    }
}

static int TrayHit(UINT32 X, UINT32 Y, UINT32 ClockX, UINT32 BarY) {
    UINT32 Nx, Nw, Ty, Th;

    DesktopNetTrayGeom(ClockX, &Nx, &Nw);
    Th = FontCellH();
    if (Th < NET_ICON) {
        Th = NET_ICON;
    }
    Ty = BarY + (TASKBAR_H > Th ? (TASKBAR_H - Th) / 2 : 0);
    return (Y >= Ty && Y < Ty + Th && X >= Nx && X < Nx + Nw) ? 1 : 0;
}

int DesktopNetTrayHandleClick(UINT32 X, UINT32 Y) {
    UINT32 Sw, Sh, BarY, ClockX, Px, Py, Pw, Ph;

    TaskbarGeom(&BarY, &Sw, &Sh);
    ClockAnchor(&ClockX);

    if (DesktopNetTrayIsOpen()) {
        PopupGeom(&Px, &Py, &Pw, &Ph);
        if (X >= Px && X < Px + Pw && Y >= Py && Y < Py + Ph) {
            return 1;
        }
        DesktopNetTraySetOpen(0);
        RequestRefresh();
        if (Y >= BarY && Y < Sh) {
            return 1;
        }
        return 0;
    }

    if (Y < BarY || Y >= Sh || !TrayHit(X, Y, ClockX, BarY)) {
        return 0;
    }
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    DesktopNetTraySetOpen(1);
    RequestRefresh();
    return 1;
}
