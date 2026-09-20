/*
 * HalRtc.c — PR-G-taskbar-clock：墙钟（CMOS 0x70/71）
 *
 * 禁止 UEFI Runtime GetTime：真机进桌面后 GuiPollMouse→DesktopTickClock
 * 每帧调用；GetTime 挂起则键鼠/电源键全死（PHOTO 期 k/m 仍正常）。
 *
 * QEMU/真机 CMOS 多按 UTC 存（Linux 默认）；课堂统一按 CST(+8) 显示。
 * 与是否联网无关，不依赖 NTP。
 */
#include "Hal.h"

#define TOY_TZ_CST_MINUTES (-480)

static UINT8 CmosRead(UINT8 Reg) {
    HalIoWrite8(0x70, (UINT8)(Reg & 0x7F));
    return HalIoRead8(0x71);
}

static UINT8 BcdToBin(UINT8 V) {
    return (UINT8)(((V >> 4) * 10) + (V & 0x0F));
}

static void ApplyUtcOffset(INT16 TzMinutes, UINT8 *Hour, UINT8 *Minute) {
    int Total;

    if (!Hour || !Minute) {
        return;
    }
    Total = (int)(*Hour) * 60 + (int)(*Minute) - (int)TzMinutes;
    while (Total < 0) {
        Total += 24 * 60;
    }
    while (Total >= 24 * 60) {
        Total -= 24 * 60;
    }
    *Hour = (UINT8)(Total / 60);
    *Minute = (UINT8)(Total % 60);
}

int HalRtcGetTime(UINT16 *Year, UINT8 *Month, UINT8 *Day,
                  UINT8 *Hour, UINT8 *Minute, UINT8 *Second) {
    UINT8 Sec, Min, Hr, Dom, Mon, Yr, Cent;
    UINT8 StatusB;
    int i;

    for (i = 0; i < 1000; i++) {
        if ((CmosRead(0x0A) & 0x80) == 0) {
            break;
        }
    }
    Sec = CmosRead(0x00);
    Min = CmosRead(0x02);
    Hr = CmosRead(0x04);
    Dom = CmosRead(0x07);
    Mon = CmosRead(0x08);
    Yr = CmosRead(0x09);
    Cent = CmosRead(0x32);
    StatusB = CmosRead(0x0B);

    if ((StatusB & 0x04) == 0) {
        Sec = BcdToBin(Sec);
        Min = BcdToBin(Min);
        Hr = BcdToBin(Hr & 0x7F);
        Dom = BcdToBin(Dom);
        Mon = BcdToBin(Mon);
        Yr = BcdToBin(Yr);
        Cent = BcdToBin(Cent);
    } else {
        Hr = (UINT8)(Hr & 0x7F);
    }
    if (Sec > 59 || Min > 59 || Hr > 23 || Mon < 1 || Mon > 12 || Dom < 1 || Dom > 31) {
        return -1;
    }
    /* 课堂默认东八区；CMOS 当 UTC（与联网无关） */
    ApplyUtcOffset(TOY_TZ_CST_MINUTES, &Hr, &Min);
    if (Year) {
        *Year = (UINT16)((Cent ? Cent : 20) * 100 + Yr);
    }
    if (Month) {
        *Month = Mon;
    }
    if (Day) {
        *Day = Dom;
    }
    if (Hour) {
        *Hour = Hr;
    }
    if (Minute) {
        *Minute = Min;
    }
    if (Second) {
        *Second = Sec;
    }
    return 0;
}
