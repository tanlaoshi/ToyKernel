/* DesktopNetTray.c — BOX-3 网态三图标；默认藏 IP；dbset net.tray.ip 1 才显示 */
#include "DesktopPrivate.h"
#include "HalDevices.h"
#include "NetConfig.h"

#define NET_TRAY_GAP 12u
#define NET_ICON     16u

static int gNetTrayOpen;
static NET_TRAY_KIND gNetKindCache = NET_TRAY_NONE;
static int gShowIpCache;
static UINT32 gIpCache;

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

static int ShowIpEnabled(void) {
    char Val[8];

    Val[0] = 0;
    if (DbGet("net.tray.ip", Val, sizeof(Val)) != DB_OK) {
        return 0;
    }
    return (Val[0] == '1' && Val[1] == 0) ? 1 : 0;
}

UINT32 DesktopNetTrayCurrentIp(void) {
    /*
     * 只信 NetConfig（真机未 DHCP=0；QEMU Ensure 后为 10.0.2.15）。
     * 勿回落 HalNetGetIpAddress：后端默认常残留 SLIRP 10.0.2.15，
     * NUC 上会误判「通」并配上 Wi‑Fi 图标。
     */
    return NetConfigGetIp();
}

NET_TRAY_KIND DesktopNetTrayDetectKind(void) {
    int Up = 0;
    UINT32 Mbps = 0;
    int Fd = 0;

    /* 不通：无 NIC / link down / 尚无真实地址 → 不通图标 */
    if (!HalNetReady()) {
        return NET_TRAY_NONE;
    }
    if (HalNetGetLinkInfo(&Up, &Mbps, &Fd) && !Up) {
        return NET_TRAY_NONE;
    }
    if (DesktopNetTrayCurrentIp() == 0) {
        return NET_TRAY_NONE;
    }
    /* 通：仅已关联的 iwl/USB-wifi 出 Wi‑Fi；Ready≠通（Probe 不够） */
    if (HalIwlAssociated() || HalWifiReady()) {
        return NET_TRAY_WIFI;
    }
    return NET_TRAY_WIRED;
}

void DesktopNetTrayMakePrefixed(char *Out, int Max, const char *Prefix, UINT32 Ip) {
    char IpBuf[16];
    int i;
    int j = 0;

    if (!Out || Max <= 0) {
        return;
    }
    if (Ip == 0) {
        StrCopy(IpBuf, (int)sizeof(IpBuf), "unset");
    } else {
        HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
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

/* 与任务栏同路径 UiFill，避免半透栏上 HalVideoFillRect 看不见 */
static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    if (W && H) {
        UiFillRectangle(X, Y, W, H, C);
    }
}

static void DrawIconNone(UINT32 X, UINT32 Y, UINT32 Fg) {
    Fill(X + 2, Y + 2, 12, 3, Fg);
    Fill(X + 2, Y + 11, 12, 3, Fg);
    Fill(X + 6, Y + 2, 4, 12, Fg);
}
static void DrawIconWired(UINT32 X, UINT32 Y, UINT32 Fg) {
    Fill(X + 5, Y + 1, 6, 9, Fg);
    Fill(X + 3, Y + 9, 10, 4, Fg);
    Fill(X + 1, Y + 13, 4, 2, Fg);
    Fill(X + 11, Y + 13, 4, 2, Fg);
}
static void DrawIconWifi(UINT32 X, UINT32 Y, UINT32 Fg) {
    Fill(X + 6, Y + 12, 4, 3, Fg);
    Fill(X + 4, Y + 8, 8, 3, Fg);
    Fill(X + 2, Y + 4, 12, 3, Fg);
    Fill(X + 0, Y + 1, 16, 2, Fg);
}

void DesktopNetTrayClose(void) {
    if (!gNetTrayOpen) {
        return;
    }
    gNetTrayOpen = 0;
    RequestRefresh();
}

void DesktopNetTraySetOpen(int Open) {
    gNetTrayOpen = Open ? 1 : 0;
}

int DesktopNetTrayIsOpen(void) {
    return gNetTrayOpen;
}

void DesktopNetTrayGeom(UINT32 ClockX, UINT32 *OutX, UINT32 *OutW) {
    UINT32 W = NET_ICON;
    char Ip[20];
    UINT32 Addr;

    if (ShowIpEnabled()) {
        Addr = DesktopNetTrayCurrentIp();
        if (Addr != 0) {
            HalNetFormatIp(Addr, Ip, (int)sizeof(Ip));
            W += FontStringWidth(Ip) + 4u;
        }
    }
    if (OutW) {
        *OutW = W;
    }
    if (OutX) {
        *OutX = (ClockX > W + NET_TRAY_GAP) ? (ClockX - W - NET_TRAY_GAP) : 0;
    }
}

void DesktopNetTrayDraw(UINT32 ClockX, UINT32 TextY) {
    UINT32 X;
    UINT32 W;
    NET_TRAY_KIND Kind;
    UINT32 Fg = ThemeClockText();
    UINT32 IconY;
    char Ip[20];
    UINT32 Addr;
    int Show;

    Kind = DesktopNetTrayDetectKind();
    Show = ShowIpEnabled();
    Addr = DesktopNetTrayCurrentIp();
    gNetKindCache = Kind;
    gShowIpCache = Show;
    gIpCache = Addr;

    DesktopNetTrayGeom(ClockX, &X, &W);
    IconY = TextY + (FontCellH() > NET_ICON ? (FontCellH() - NET_ICON) / 2 : 0);
    if (Kind == NET_TRAY_WIRED) {
        DrawIconWired(X, IconY, Fg);
    } else if (Kind == NET_TRAY_WIFI) {
        DrawIconWifi(X, IconY, Fg);
    } else {
        DrawIconNone(X, IconY, Fg);
    }
    if (Show && Addr != 0) {
        HalNetFormatIp(Addr, Ip, (int)sizeof(Ip));
        HalVideoDrawStringAt(X + NET_ICON + 4u, TextY, Ip, Fg);
    }
}

int DesktopNetTrayLabelChanged(void) {
    NET_TRAY_KIND Kind = DesktopNetTrayDetectKind();
    int Show = ShowIpEnabled();
    UINT32 Ip = DesktopNetTrayCurrentIp();

    if (Kind == gNetKindCache && Show == gShowIpCache && Ip == gIpCache) {
        return 0;
    }
    gNetKindCache = Kind;
    gShowIpCache = Show;
    gIpCache = Ip;
    return 1;
}
