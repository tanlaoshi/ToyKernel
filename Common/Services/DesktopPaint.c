/*
 * DesktopPaint.c — 桌面绘制（图标/任务栏/开始菜单）（PR-S-desktop-split-2）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。
 */
#include "DesktopPriv.h"

void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (W == 0 || H == 0) {
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
                HalVideoFillRect(X + RunStart, Y + Row, Col - RunStart, 1, Color);
                InRun = 0;
            }
        }
        if (InRun) {
            HalVideoFillRect(X + RunStart, Y + Row, W - RunStart, 1, Color);
        }
    }
}

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

void DrawStringFree(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cx = X;
    UINT32 CellH;

    if (!Text) {
        return;
    }
    CellH = FontCellH();
    while (*Text) {
        UINT32 Cp;
        UINTN N;
        UINT32 Adv;
        char One[5];
        UINTN k;
        UINT32 Row;
        UINT32 Col;
        int Free;

        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        Adv = FontCodepointAdvance(Cp);
        if (Adv == 0) {
            Adv = FontCellW();
        }
        for (k = 0; k < N && k < sizeof(One) - 1; k++) {
            One[k] = Text[k];
        }
        One[k] = 0;
        /*
         * 旧逻辑只测左上角：字形会画进标题栏留下黄/白烙印。
         * 单元格任一像素被窗占用则整字跳过。
         */
        Free = 1;
        for (Row = 0; Free && Row < CellH; Row++) {
            for (Col = 0; Col < Adv; Col++) {
                if (PointOccupied(Cx + Col, Y + Row)) {
                    Free = 0;
                    break;
                }
            }
        }
        if (Free) {
            HalVideoDrawStringAt(Cx, Y, One, Color);
        }
        Cx += Adv;
        Text += N;
    }
}

void DrawOneIconRaw(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    BlitIconFaceRaw(Icon->X, Icon->Y, Icon);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
    UiDrawRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Border);
    if (Selected) {
        UiDrawRectangle(Icon->X + 1, Icon->Y + 1,
                        DESKTOP_ICON_SIZE - 2, DESKTOP_ICON_SIZE - 2,
                        COLOR_YELLOW);
    }

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    LabelX = Icon->X;
    if (LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    if (Icon->Label) {
        HalVideoDrawStringAt(LabelX, LabelY, Icon->Label,
                             Selected ? COLOR_YELLOW : COLOR_WHITE);
    }
}

void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    BlitIconFaceFree(Icon->X, Icon->Y, Icon);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, 1, Border);
    FillRectFree(Icon->X, Icon->Y + DESKTOP_ICON_SIZE - 1, DESKTOP_ICON_SIZE, 1,
                 Border);
    FillRectFree(Icon->X, Icon->Y, 1, DESKTOP_ICON_SIZE, Border);
    FillRectFree(Icon->X + DESKTOP_ICON_SIZE - 1, Icon->Y, 1, DESKTOP_ICON_SIZE,
                 Border);
    if (Selected) {
        FillRectFree(Icon->X + 1, Icon->Y + 1, DESKTOP_ICON_SIZE - 2, 1, Border);
        FillRectFree(Icon->X + 1, Icon->Y + DESKTOP_ICON_SIZE - 2,
                     DESKTOP_ICON_SIZE - 2, 1, Border);
    }

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    LabelX = Icon->X;
    if (LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    if (Icon->Label) {
        DrawStringFree(LabelX, LabelY, Icon->Label,
                       Selected ? COLOR_YELLOW : COLOR_WHITE);
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
                         gMenuOpen ? ThemeWindowTitleText() : COLOR_BLACK);

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
    HalVideoDrawStringAt(ClockX, Ty, Clock, COLOR_WHITE);
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
    UiDrawRectangle(Mx, My, Mw, Mh, COLOR_BLACK);
    for (i = 0; i < gMenuCount; i++) {
        MENU_ROW *R = &gMenuRows[i];
        UINT32 Iy = My + (UINT32)i * MENU_ITEM_H;
        UINT32 IconX;
        UINT32 IconY;
        UINT32 TextX;
        UINT32 Fg;
        int HasIcon = 0;

        UiDrawRectangle(Mx, Iy, Mw, MENU_ITEM_H, ThemeWindowBorderIdle());
        IconX = Mx + 6;
        IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ ? (MENU_ITEM_H - MENU_ICON_SZ) / 2 : 0);
        TextX = Mx + 10;
        Fg = R->Enabled ? COLOR_BLACK : ThemeControlBorder();
        if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT &&
            gIcons[R->IconSrc].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[R->IconSrc].Bmp);
            HasIcon = 1;
        } else if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[R->IconSrc].IconColor);
            HasIcon = 1;
        } else if (R->IconSrc == 4 && gPowerBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gPowerBmp);
            HasIcon = 1;
        } else if (R->IconSrc == 4) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, 0x00C04040);
            HasIcon = 1;
        } else if (R->IconSrc == 5 && gRebootBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gRebootBmp);
            HasIcon = 1;
        } else if (R->IconSrc == 5) {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, 0x00C08020);
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
        if (R->Action == DESKTOP_ACTION_APPS) {
            HalVideoDrawStringAt(Mx + Mw - 14u,
                                 Iy + (MENU_ITEM_H > FontCellH()
                                           ? (MENU_ITEM_H - FontCellH()) / 2
                                           : 0),
                                 gMenuAppsOpen ? "v" : ">", Fg);
        }
    }

    if (gMenuAppsOpen) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;
        int Rows;

        AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
        UiFillRectangle(Fx, Fy, Fw, Fh, ThemeControlFace());
        UiDrawRectangle(Fx, Fy, Fw, Fh, COLOR_BLACK);
        Rows = gMenuAppCount > 0 ? gMenuAppCount : 1;
        for (i = 0; i < Rows; i++) {
            MENU_ROW *R;
            UINT32 Iy = Fy + (UINT32)i * MENU_ITEM_H;
            UINT32 IconX;
            UINT32 IconY;
            UINT32 TextX;
            UINT32 Fg;
            const char *Lab;

            UiDrawRectangle(Fx, Iy, Fw, MENU_ITEM_H, ThemeWindowBorderIdle());
            if (gMenuAppCount <= 0) {
                HalVideoDrawStringAt(Fx + 10u,
                                     Iy + (MENU_ITEM_H > FontCellH()
                                               ? (MENU_ITEM_H - FontCellH()) / 2
                                               : 0),
                                     "(empty)", ThemeControlBorder());
                break;
            }
            R = &gMenuAppRows[i];
            Lab = R->Label[0] ? R->Label : "?";
            Fg = R->Enabled ? COLOR_BLACK : ThemeControlBorder();
            IconX = Fx + 6;
            IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ
                              ? (MENU_ITEM_H - MENU_ICON_SZ) / 2
                              : 0);
            TextX = Fx + 10;
            if (gIcons[0].BmpReady) {
                BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                                 &gIcons[0].Bmp);
                TextX = IconX + MENU_ICON_SZ + 6u;
            } else {
                UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                                gIcons[0].IconColor);
                TextX = IconX + MENU_ICON_SZ + 6u;
            }
            HalVideoDrawStringAt(TextX,
                                 Iy + (MENU_ITEM_H > FontCellH()
                                           ? (MENU_ITEM_H - FontCellH()) / 2
                                           : 0),
                                 Lab, Fg);
        }
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
