/*
 * DesktopNetTray.c — 任务栏时钟左侧网络短状态（PR-N-nic-tray）
 */
#include "DesktopPrivate.h"
#include "HalDevices.h"
#include "NetConfig.h"

#define NET_TRAY_GAP   12u
#define NET_POP_PAD    8u
#define NET_POP_W      200u
#define NET_POP_LINES  5
#define NET_LABEL_MAX  20

static int gNetTrayOpen;
static char gNetLabelCache[NET_LABEL_MAX];

static void StrCopy(char *Dst, int Max, const char *Src) {
    int i = 0;
    if (!Dst || Max <= 0) {
        return;
    }
    while (Src && Src[i] && i < Max - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static int StrEq(const char *A, const char *B) {
    int i;
    if (!A || !B) {
        return 0;
    }
    for (i = 0; A[i] || B[i]; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
    }
    return 1;
}

static void FormatIpOrUnset(UINT32 Ip, char *Buf, int Max) {
    if (!Buf || Max < 8) {
        return;
    }
    if (Ip == 0) {
        StrCopy(Buf, Max, "unset");
        return;
    }
    HalNetFormatIp(Ip, Buf, Max);
}

static void BuildShortLabel(char *Buf, int Max) {
    int Up = 0;
    UINT32 Mbps = 0;
    int Fd = 0;
    UINT32 Ip;

    if (!Buf || Max < 4) {
        return;
    }
    if (!HalNetReady()) {
        StrCopy(Buf, Max, "net-");
        return;
    }
    if (HalNetGetLinkInfo(&Up, &Mbps, &Fd) && !Up) {
        StrCopy(Buf, Max, "down");
        return;
    }
    Ip = HalNetGetIpAddress();
    if (Ip == 0) {
        Ip = NetConfigGetIp();
    }
    if (Ip == 0) {
        StrCopy(Buf, Max, "up");
        return;
    }
    HalNetFormatIp(Ip, Buf, Max);
}

static void MakePrefixed(char *Out, int Max, const char *Prefix, UINT32 Ip) {
    char IpBuf[16];
    int i;
    int j = 0;

    FormatIpOrUnset(Ip, IpBuf, (int)sizeof(IpBuf));
    if (!Out || Max <= 0) {
        return;
    }
    while (Prefix && Prefix[j] && j < Max - 1) {
        Out[j] = Prefix[j];
        j++;
    }
    for (i = 0; IpBuf[i] && j < Max - 1; i++) {
        Out[j++] = IpBuf[i];
    }
    Out[j] = 0;
}

static void ClockAnchor(UINT32 *ClockXOut) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 ClockW;
    char Clock[8];

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)BarY;
    StrCopy(Clock, (int)sizeof(Clock), "00:00");
    ClockW = FontStringWidth(Clock);
    *ClockXOut = (Sw > ClockW + 12u) ? (Sw - ClockW - 12u) : 0;
}

void DesktopNetTrayClose(void) {
    if (!gNetTrayOpen) {
        return;
    }
    gNetTrayOpen = 0;
    RequestRefresh();
}

int DesktopNetTrayIsOpen(void) {
    return gNetTrayOpen;
}

void DesktopNetTrayGeom(UINT32 ClockX, UINT32 *OutX, UINT32 *OutW) {
    char Label[NET_LABEL_MAX];
    UINT32 W;

    BuildShortLabel(Label, (int)sizeof(Label));
    W = FontStringWidth(Label);
    if (OutW) {
        *OutW = W;
    }
    if (OutX) {
        *OutX = (ClockX > W + NET_TRAY_GAP) ? (ClockX - W - NET_TRAY_GAP) : 0;
    }
}

void DesktopNetTrayDraw(UINT32 ClockX, UINT32 TextY) {
    char Label[NET_LABEL_MAX];
    UINT32 X;
    UINT32 W;

    BuildShortLabel(Label, (int)sizeof(Label));
    StrCopy(gNetLabelCache, (int)sizeof(gNetLabelCache), Label);
    DesktopNetTrayGeom(ClockX, &X, &W);
    HalVideoDrawStringAt(X, TextY, Label, COLOR_WHITE);
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

    if (!gNetTrayOpen) {
        return;
    }
    PopupGeom(&Px, &Py, &Pw, &Ph);
    UiFillRectangle(Px, Py, Pw, Ph, ThemeControlFace());
    UiDrawRectangle(Px, Py, Pw, Ph, COLOR_BLACK);

    Ty = Py + NET_POP_PAD;
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, "Network", COLOR_BLACK);
    Ty += FontCellH();

    Ip = HalNetReady() ? HalNetGetIpAddress() : 0;
    MakePrefixed(Line, (int)sizeof(Line), "ip  ", Ip);
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, COLOR_DARK_GRAY);
    Ty += FontCellH();

    MakePrefixed(Line, (int)sizeof(Line), "gw  ", NetConfigGetGw());
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, COLOR_DARK_GRAY);
    Ty += FontCellH();

    MakePrefixed(Line, (int)sizeof(Line), "dns ", NetConfigGetDns());
    HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, Line, COLOR_DARK_GRAY);
    Ty += FontCellH();

    if (!HalNetReady()) {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, "link n/a", COLOR_DARK_GRAY);
    } else if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty,
                             Up ? "link up" : "link down", COLOR_DARK_GRAY);
    } else {
        HalVideoDrawStringAt(Px + NET_POP_PAD, Ty, "link n/a", COLOR_DARK_GRAY);
    }
}

int DesktopNetTrayHit(UINT32 X, UINT32 Y, UINT32 ClockX, UINT32 BarY) {
    UINT32 Nx, Nw, Ty, Th;

    DesktopNetTrayGeom(ClockX, &Nx, &Nw);
    Th = FontCellH();
    Ty = BarY + (TASKBAR_H > Th ? (TASKBAR_H - Th) / 2 : 0);
    return (Y >= Ty && Y < Ty + Th && X >= Nx && X < Nx + Nw) ? 1 : 0;
}

int DesktopNetTrayHandleClick(UINT32 X, UINT32 Y) {
    UINT32 Sw, Sh, BarY, ClockX, Px, Py, Pw, Ph;

    TaskbarGeom(&BarY, &Sw, &Sh);
    ClockAnchor(&ClockX);

    if (gNetTrayOpen) {
        PopupGeom(&Px, &Py, &Pw, &Ph);
        if (X >= Px && X < Px + Pw && Y >= Py && Y < Py + Ph) {
            return 1;
        }
        gNetTrayOpen = 0;
        RequestRefresh();
        if (Y >= BarY && Y < Sh) {
            return 1;
        }
        return 0;
    }

    if (Y < BarY || Y >= Sh || !DesktopNetTrayHit(X, Y, ClockX, BarY)) {
        return 0;
    }
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gNetTrayOpen = 1;
    RequestRefresh();
    return 1;
}

int DesktopNetTrayLabelChanged(void) {
    char Label[NET_LABEL_MAX];

    BuildShortLabel(Label, (int)sizeof(Label));
    if (StrEq(Label, gNetLabelCache)) {
        return 0;
    }
    StrCopy(gNetLabelCache, (int)sizeof(gNetLabelCache), Label);
    return 1;
}
