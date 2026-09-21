/*
 * DesktopTaskbar.c — 任务栏与开始菜单绘制
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

/* PR-GUI-alpha：遮挡路径半透明填充（自由像素 Src-over） */
static void FillRectFreeAlpha(UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                              UINT32 Color, UINT8 Alpha) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0 || Alpha == 0) {
        return;
    }
    if (Alpha == 255) {
        FillRectFree(X, Y, W, H, Color);
        return;
    }
    for (Row = 0; Row < H; Row++) {
        InRun = 0;
        RunStart = 0;
        for (Col = 0; Col < W; Col++) {
            int Free = !PointOccupied(X + Col, Y + Row);
            if (Free && !InRun) {
                RunStart = Col;
                InRun = 1;
            } else if (!Free && InRun) {
                HalVideoBlendFillRect(X + RunStart, Y + Row, Col - RunStart, 1,
                                      Color, Alpha);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoBlendFillRect(X + RunStart, Y + Row, W - RunStart, 1,
                                  Color, Alpha);
        }
    }
}

/* 任务栏控件（边框/开始钮/时钟）；底色由调用方 Fill / BlendFill */
static void DrawTaskbarControls(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Tx;
    UINT32 Ty;
    UINT32 Ix;
    UINT32 Iy;
    UINT32 ClockW;
    UINT32 ClockX;
    const char *Start;
    char Clock[8];
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int HaveTime;

    TaskbarGeom(&BarY, &Sw, &Sh);
    StartBtnGeom(&Bx, &By, &Bw, &Bh);
    Start = LocStr(MSG_START);

    UiDrawRectangle(0, BarY, Sw, TASKBAR_H, ThemeWindowBorderIdle());
    UiFillRectangle(Bx, By, Bw, Bh,
                    gMenuOpen ? ThemeTaskbarButtonActive() : ThemeTaskbarButton());
    UiDrawRectangle(Bx, By, Bw, Bh, ThemeWindowBorderFocus());

    Ix = Bx + START_BTN_PAD_X;
    Iy = By + (Bh > START_ICON_SZ ? (Bh - START_ICON_SZ) / 2 : 0);
    if (gStartBmpReady) {
        BlitBmpScaledRaw(Ix, Iy, START_ICON_SZ, START_ICON_SZ, &gStartBmp);
    } else {
        UiFillRectangle(Ix, Iy, START_ICON_SZ, START_ICON_SZ, ThemeTaskbarButtonActive());
    }
    Tx = Ix + START_ICON_SZ + 6u;
    Ty = BarY + (TASKBAR_H > FontCellH() ? (TASKBAR_H - FontCellH()) / 2 : 0);
    HalVideoDrawStringAt(Tx, Ty, Start ? Start : "Start",
                         gMenuOpen ? ThemeWindowTitleText() : ThemeStartButtonText());

    /* 右下角 HH:MM（CMOS+CST）；失败则 --:-- */
    HaveTime = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (HaveTime) {
        Clock[0] = (char)('0' + (Hour / 10) % 10);
        Clock[1] = (char)('0' + (Hour % 10));
        Clock[2] = ':';
        Clock[3] = (char)('0' + (Minute / 10) % 10);
        Clock[4] = (char)('0' + (Minute % 10));
        Clock[5] = 0;
        gClockHour = Hour;
        gClockMinute = Minute;
        gClockValid = 1;
    } else {
        Clock[0] = '-';
        Clock[1] = '-';
        Clock[2] = ':';
        Clock[3] = '-';
        Clock[4] = '-';
        Clock[5] = 0;
        gClockValid = 0;
    }
    ClockW = FontStringWidth(Clock);
    ClockX = (Sw > ClockW + 12u) ? (Sw - ClockW - 12u) : Bx + Bw + 8u;
    DesktopNetTrayDraw(ClockX, Ty);
    HalVideoDrawStringAt(ClockX, Ty, Clock, ThemeClockText());
}

void DrawTaskbarRaw(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;

    TaskbarGeom(&BarY, &Sw, &Sh);
    UiFillRectangleAlpha(0, BarY, Sw, TASKBAR_H, ThemeTaskbarBackground(),
                         ThemeTaskbarAlpha());
    DrawTaskbarControls();
}

static void DrawFlyoutBox(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh,
                          MENU_ROW *Rows, int Count, int IconIdx) {
    int i;
    int N = Count > 0 ? Count : 1;

    UiFillRectangle(Fx, Fy, Fw, Fh, ThemeControlFace());
    UiDrawRectangle(Fx, Fy, Fw, Fh, ThemeMenuBorder());
    for (i = 0; i < N; i++) {
        MENU_ROW *R;
        UINT32 Iy = Fy + (UINT32)i * MENU_ITEM_H;
        UINT32 IconX;
        UINT32 IconY;
        UINT32 TextX;
        UINT32 Fg;
        const char *Lab;
        int UseIcon = (IconIdx >= 0 && IconIdx < DESKTOP_ICON_COUNT);

        UiDrawRectangle(Fx, Iy, Fw, MENU_ITEM_H, ThemeMenuSep());
        if (Count <= 0) {
            HalVideoDrawStringAt(Fx + 10u,
                                 Iy + (MENU_ITEM_H > FontCellH()
                                           ? (MENU_ITEM_H - FontCellH()) / 2
                                           : 0),
                                 "(empty)", ThemeControlBorder());
            break;
        }
        R = &Rows[i];
        Lab = R->Label[0] ? R->Label : "?";
        Fg = R->Enabled ? ThemeMenuText() : ThemeControlBorder();
        IconX = Fx + 6;
        IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ ? (MENU_ITEM_H - MENU_ICON_SZ) / 2 : 0);
        TextX = Fx + 10;
        if (UseIcon && gIcons[IconIdx].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[IconIdx].Bmp);
            TextX = IconX + MENU_ICON_SZ + 6u;
        } else if (UseIcon) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[IconIdx].IconColor);
            TextX = IconX + MENU_ICON_SZ + 6u;
        }
        HalVideoDrawStringAt(TextX,
                             Iy + (MENU_ITEM_H > FontCellH()
                                       ? (MENU_ITEM_H - FontCellH()) / 2
                                       : 0),
                             Lab, Fg);
    }
}

void DrawStartMenuRaw(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int i;

    if (!gMenuOpen) {
        return;
    }
    if (gMenuCount <= 0) {
        RebuildStartMenu();
    }
    MenuGeom(&Mx, &My, &Mw, &Mh);
    /* 实心面板：勿半透叠窗，否则备份/刷新易留烙印 */
    UiFillRectangle(Mx, My, Mw, Mh, ThemeControlFace());
    UiDrawRectangle(Mx, My, Mw, Mh, ThemeMenuBorder());
    for (i = 0; i < gMenuCount; i++) {
        MENU_ROW *R = &gMenuRows[i];
        UINT32 Iy = My + (UINT32)i * MENU_ITEM_H;
        UINT32 IconX;
        UINT32 IconY;
        UINT32 TextX;
        UINT32 Fg;
        int HasIcon = 0;

        UiDrawRectangle(Mx, Iy, Mw, MENU_ITEM_H, ThemeMenuSep());
        IconX = Mx + 6;
        IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ ? (MENU_ITEM_H - MENU_ICON_SZ) / 2 : 0);
        TextX = Mx + 10;
        Fg = R->Enabled ? ThemeMenuText() : ThemeControlBorder();
        if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT &&
            gIcons[R->IconSrc].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[R->IconSrc].Bmp);
            HasIcon = 1;
        } else if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[R->IconSrc].IconColor);
            HasIcon = 1;
        } else if (R->IconSrc == 6 && gPowerBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gPowerBmp);
            HasIcon = 1;
        } else if (R->IconSrc == 6) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, ThemeIconFallbackPower());
            HasIcon = 1;
        } else if (R->IconSrc == 7 && gRebootBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gRebootBmp);
            HasIcon = 1;
        } else if (R->IconSrc == 7) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, ThemeIconFallbackReboot());
            HasIcon = 1;
        } else if (R->Action == DESKTOP_ACTION_EXEC && gIcons[0].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[0].Bmp);
            HasIcon = 1;
        } else if (R->Action == DESKTOP_ACTION_EXEC) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[0].IconColor);
            HasIcon = 1;
        }
        if (HasIcon) {
            TextX = IconX + MENU_ICON_SZ + 6u;
        }
        HalVideoDrawStringAt(TextX,
                             Iy + (MENU_ITEM_H > FontCellH()
                                       ? (MENU_ITEM_H - FontCellH()) / 2
                                       : 0),
                             R->Label[0] ? R->Label : "?", Fg);
        if (R->Action == DESKTOP_ACTION_APPS || R->Action == DESKTOP_ACTION_GAME) {
            int Open = (R->Action == DESKTOP_ACTION_APPS) ? gMenuAppsOpen
                                                         : gMenuGameOpen;

            HalVideoDrawStringAt(Mx + Mw - 14u,
                                 Iy + (MENU_ITEM_H > FontCellH()
                                           ? (MENU_ITEM_H - FontCellH()) / 2
                                           : 0),
                                 Open ? "v" : ">", Fg);
        }
    }

    if (gMenuAppsOpen) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;

        AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
        DrawFlyoutBox(Fx, Fy, Fw, Fh, gMenuAppRows, gMenuAppCount, 0);
    }
    if (gMenuGameOpen) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;

        GameFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
        DrawFlyoutBox(Fx, Fy, Fw, Fh, gMenuGameRows, gMenuGameCount, 5);
    }
}

void DrawTaskbarOccluded(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;

    TaskbarGeom(&BarY, &Sw, &Sh);
    FillRectFreeAlpha(0, BarY, Sw, TASKBAR_H, ThemeTaskbarBackground(),
                      ThemeTaskbarAlpha());
    /* 控件：勿再调 DrawTaskbarRaw（会双重 Blend 底色） */
    DrawTaskbarControls();
    if (gMenuOpen) {
        DrawStartMenuRaw();
    }
}
