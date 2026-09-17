/*
 * Desktop.c — 桌面图标 + 任务栏/开始菜单 + BMP 壁纸/图标（PR-D4 / PR-G13）
 *
 * 开窗：桌面双击图标，或任务栏「开始」菜单（不单靠图标）。
 * PR-G-desk-1：图标可拖放；松手写入 TOYOS.DB（ic0..ic3=x,y）；启动时 LoadIconLayout。
 * PR-G-desk-2：开始菜单动态列出 Apps/ 下 .ELF + 缺文件 INST(app) 灰显；点选 ProcessExec。
 * 壁纸/图标：优先 TOYOS:Assets/…（BI_RGB BMP）；读不到则纯色块（不内嵌像素、不走 RES:）。
 */
#include "DesktopPriv.h"

/* 全局定义集中在宿主；其它 TU 经 DesktopPriv.h extern */
MENU_ROW gMenuRows[MENU_ROWS_MAX];
int gMenuCount;
FAT_DIRECTORY_ENTRY gMenuDirScratch[FAT_LIST_MAX];
STORE_INSTALLED gMenuInstScratch[STORE_INSTALLED_MAX];

DESKTOP_ICON gIcons[DESKTOP_ICON_COUNT];
int gDeskSelected = -1;
UINT64 gSelectClock;
UINT32 gSelectX;
UINT32 gSelectY;

/* PR-G-desk-1：图标拖放状态 */
int gIconDragIdx = -1;
INT32 gIconDragOffX;
INT32 gIconDragOffY;
UINT32 gIconDragStartX;
UINT32 gIconDragStartY;
int gIconDragMoved;

BMP_IMAGE gWall;
int gWallReady;
BMP_IMAGE gStartBmp;
int gStartBmpReady;
BMP_IMAGE gPowerBmp;
int gPowerBmpReady;
BMP_IMAGE gRebootBmp;
int gRebootBmpReady;
int gMenuOpen;
UINT8 gClockHour;
UINT8 gClockMinute;
int gClockValid;

/* 已按当前分辨率拉伸的壁纸缓存（加速 DesktopFillRect，避免拖死鼠标） */
UINT32 *gWallScreen;
UINT32  gWallScreenW;
UINT32  gWallScreenH;
UINT32  gWallScreenPages;
int     gDesktopBusy; /* 防 DesktopInit / OnDisplayResize 重入 */

/* PR-R2：由 Gui 注册，Desktop 不 include Gui.h */
int (*gPointOccupied)(UINT32 X, UINT32 Y);
void (*gRequestRefresh)(void);

int PointOccupied(UINT32 X, UINT32 Y) {
    return gPointOccupied ? gPointOccupied(X, Y) : 0;
}

void RequestRefresh(void) {
    if (gRequestRefresh) {
        gRequestRefresh();
    }
}

void DesktopSetPointOccupied(int (*Fn)(UINT32 X, UINT32 Y)) {
    gPointOccupied = Fn;
}

void DesktopSetRequestRefresh(void (*Fn)(void)) {
    gRequestRefresh = Fn;
}

UINT64 DesktopClock(void) {
    return HalCpuTicks(0);
}

int RectsOverlap(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                        UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Ax + Aw > Bx && Ay < By + Bh && Ay + Ah > By;
}

void IconBounds(const DESKTOP_ICON *Icon, UINT32 *X, UINT32 *Y,
                       UINT32 *W, UINT32 *H) {
    UINT32 LabelW;
    UINT32 TotalW;
    UINT32 TotalH;

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    TotalW = DESKTOP_ICON_SIZE;
    if (LabelW + 4 > TotalW) {
        TotalW = LabelW + 4;
    }
    TotalH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH();
    *X = Icon->X;
    *Y = Icon->Y;
    *W = TotalW;
    *H = TotalH;
}

int PointInIcon(const DESKTOP_ICON *Icon, UINT32 X, UINT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;

    IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
    return X >= Ix && X < Ix + Iw && Y >= Iy && Y < Iy + Ih;
}

void TaskbarGeom(UINT32 *BarY, UINT32 *Sw, UINT32 *Sh) {
    HalVideoGetSize(Sw, Sh);
    *BarY = (*Sh > TASKBAR_H) ? (*Sh - TASKBAR_H) : 0;
}

/* 开始钮：可选 START.BMP + 文案；宽度随字体变化 */
void StartBtnGeom(UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Tw;
    UINT32 Bh;
    UINT32 IconSlot;
    const char *Start;

    TaskbarGeom(&BarY, &Sw, &Sh);
    Start = LocStr(MSG_START);
    Tw = FontStringWidth(Start ? Start : "Start");
    IconSlot = START_ICON_SZ + 6u; /* BMP 或纯色占位 */
    *OutW = Tw + START_BTN_PAD_X * 2 + IconSlot;
    if (*OutW < START_BTN_MIN_W) {
        *OutW = START_BTN_MIN_W;
    }
    if (*OutW + 8 > Sw) {
        *OutW = Sw > 8 ? Sw - 8 : Sw;
    }
    Bh = TASKBAR_H > 8 ? TASKBAR_H - 8 : TASKBAR_H;
    *OutX = 4;
    *OutY = BarY + 4;
    *OutH = Bh;
}

void MenuGeom(UINT32 *Mx, UINT32 *My, UINT32 *Mw, UINT32 *Mh) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    int Rows;

    TaskbarGeom(&BarY, &Sw, &Sh);
    Rows = gMenuCount > 0 ? gMenuCount : (MENU_FIXED_TOP + MENU_FIXED_BOT);
    *Mw = MENU_W;
    if (*Mw + 8u > Sw) {
        *Mw = Sw > 8u ? Sw - 8u : Sw;
    }
    *Mh = MENU_ITEM_H * (UINT32)Rows;
    *Mx = 4;
    *My = (BarY > *Mh) ? (BarY - *Mh) : 0;
}

void ClampIconPos(UINT32 *X, UINT32 *Y) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 MaxX;
    UINT32 MaxY;

    TaskbarGeom(&BarY, &Sw, &Sh);
    MaxX = (Sw > DESKTOP_ICON_SIZE + 4u) ? (Sw - DESKTOP_ICON_SIZE - 4u) : 0;
    MaxY = (BarY > DESKTOP_ICON_SIZE + FontCellH() + DESKTOP_LABEL_PAD + 4u)
               ? (BarY - DESKTOP_ICON_SIZE - FontCellH() - DESKTOP_LABEL_PAD - 4u)
               : 0;
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

void ClampAllIcons(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        ClampIconPos(&gIcons[i].X, &gIcons[i].Y);
    }
}

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
        ClampIconPos(&gIcons[gIconDragIdx].X, &gIcons[gIconDragIdx].Y);
        SaveIconLayout();
        RedrawIconIndex(gIconDragIdx);
        HalVideoPresent();
    }
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    gIconDragOffX = 0;
    gIconDragOffY = 0;
}

/* 只画不被窗口盖住的像素 */

void DesktopDraw(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        DrawOneIconRaw(&gIcons[i], i == gDeskSelected);
    }
    DrawTaskbarRaw();
    DrawStartMenuRaw();
}

void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    int i;
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        IconBounds(&gIcons[i], &Ix, &Iy, &Iw, &Ih);
        if (RectsOverlap(X, Y, W, H, Ix, Iy, Iw, Ih)) {
            DrawOneIconOccluded(&gIcons[i], i == gDeskSelected);
        }
    }
    TaskbarGeom(&BarY, &Sw, &Sh);
    if (RectsOverlap(X, Y, W, H, 0, BarY, Sw, TASKBAR_H)) {
        DrawTaskbarOccluded();
    } else if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (RectsOverlap(X, Y, W, H, Mx, My, Mw, Mh)) {
            DrawStartMenuRaw();
        }
    }
}

int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out) {
    int i;
    UINT32 Border;
    UINT32 LabelW;
    UINT32 LabelX;
    UINT32 LabelY;
    const char *P;
    UINT32 Cx;
    UINT32 CellH;
    const FONT_FACE *Face;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    if (!Out) {
        return 0;
    }
    CellH = FontCellH();
    Face = FontGetCurrent();

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) {
            *Out = ThemeControlFace();
            return 1;
        }
    }
    if (Y >= BarY && Y < Sh) {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;

        StartBtnGeom(&Bx, &By, &Bw, &Bh);
        if (X >= Bx && X < Bx + Bw &&
            Y >= By && Y < By + Bh) {
            *Out = gMenuOpen ? ThemeTaskbarButtonActive() : ThemeTaskbarButton();
            return 1;
        }
        *Out = ThemeTaskbarBackground();
        return 1;
    }

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const DESKTOP_ICON *Icon = &gIcons[i];
        int Selected = (i == gDeskSelected);
        UINT32 Ix;
        UINT32 Iy;
        UINT32 Iw;
        UINT32 Ih;

        IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
        if (X < Ix || Y < Iy || X >= Ix + Iw || Y >= Iy + Ih) {
            continue;
        }

        if (X >= Icon->X && Y >= Icon->Y &&
            X < Icon->X + DESKTOP_ICON_SIZE &&
            Y < Icon->Y + DESKTOP_ICON_SIZE) {
            Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
            if (X == Icon->X || Y == Icon->Y ||
                X == Icon->X + DESKTOP_ICON_SIZE - 1 ||
                Y == Icon->Y + DESKTOP_ICON_SIZE - 1) {
                *Out = Border;
                return 1;
            }
            if (Selected &&
                (X == Icon->X + 1 || Y == Icon->Y + 1 ||
                 X == Icon->X + DESKTOP_ICON_SIZE - 2 ||
                 Y == Icon->Y + DESKTOP_ICON_SIZE - 2)) {
                *Out = COLOR_YELLOW;
                return 1;
            }
            if (Icon->BmpReady && Icon->Bmp.Pixels) {
                UINT32 RelX = X - Icon->X;
                UINT32 RelY = Y - Icon->Y;
                if (RelX < Icon->Bmp.Width && RelY < Icon->Bmp.Height) {
                    *Out = Icon->Bmp.Pixels[RelY * Icon->Bmp.Width + RelX];
                    return 1;
                }
            }
            *Out = Icon->IconColor;
            return 1;
        }

        LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
        LabelX = Icon->X;
        if (LabelW < DESKTOP_ICON_SIZE) {
            LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
        }
        LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
        if (!Icon->Label || Face == 0 || Y < LabelY || Y >= LabelY + CellH) {
            continue;
        }
        P = Icon->Label;
        Cx = LabelX;
        while (P && *P) {
            UINT32 Cp;
            UINTN N;
            UINT32 Adv;
            UINT32 Gw;
            UINT32 Gh;
            const UINT8 *Glyph;

            N = Utf8Decode(P, &Cp);
            if (N == 0) {
                P++;
                continue;
            }
            Adv = FontCodepointAdvance(Cp);
            if (Cp != '\n' && X >= Cx && X < Cx + Adv) {
                UINT32 RelX;
                UINT32 RelY;
                UINT32 Gx;
                UINT32 Gy;

                Glyph = FontGlyphCp(Cp, &Gw, &Gh);
                RelX = X - Cx;
                RelY = Y - LabelY;
                if (Glyph != 0) {
                    UINT32 Bpr = (Gw + 7) / 8;
                    UINT32 Stretch = FontGlyphStretch(Gh);
                    UINT32 DrawnH;
                    UINT32 OffY = 0;

                    if (Stretch < 1) {
                        Stretch = 1;
                    }
                    DrawnH = Gh * Stretch;
                    if (CellH > DrawnH) {
                        OffY = (CellH - DrawnH) / 2;
                    }
                    if (RelY >= OffY) {
                        Gx = RelX / Stretch;
                        Gy = (RelY - OffY) / Stretch;
                        if (Gx < Gw && Gy < Gh) {
                            UINT8 Byte = Glyph[Gy * Bpr + (Gx / 8)];
                            int Bit = 7 - (int)(Gx % 8);

                            if (Byte & (1 << Bit)) {
                                *Out = Selected ? COLOR_YELLOW : COLOR_WHITE;
                                return 1;
                            }
                        }
                    }
                }
                return 0;
            }
            Cx += Adv;
            P += N;
        }
    }
    return 0;
}

void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    DrawOneIconOccluded(&gIcons[Idx], Idx == gDeskSelected);
}

void SelectIcon(int Hit, UINT32 X, UINT32 Y, UINT64 Now) {
    int Prev = gDeskSelected;

    gDeskSelected = Hit;
    gSelectClock = Now;
    gSelectX = X;
    gSelectY = Y;
    if (Prev >= 0 && Prev != Hit) {
        RedrawIconIndex(Prev);
    }
    RedrawIconIndex(Hit);
}

static int HandleTaskbarClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction,
                              char *OutExecPath, UINTN ExecPathMax) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int Item;

    if (OutExecPath && ExecPathMax > 0) {
        OutExecPath[0] = 0;
    }

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) {
            Item = (int)((Y - My) / MENU_ITEM_H);
            if (Item >= 0 && Item < gMenuCount) {
                MENU_ROW *R = &gMenuRows[Item];
                DESKTOP_ACTION Act = R->Action;

                gMenuOpen = 0;
                if (Act != DESKTOP_ACTION_SHUTDOWN &&
                    Act != DESKTOP_ACTION_REBOOT) {
                    RequestRefresh();
                }
                if (!R->Enabled) {
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (OutAction) {
                    *OutAction = Act;
                }
                if (Act == DESKTOP_ACTION_EXEC && OutExecPath &&
                    ExecPathMax > 0) {
                    MenuCopyStr(OutExecPath, (int)ExecPathMax, R->Path);
                }
                return 1;
            }
        }
        /* 点在菜单外：关菜单并刷新 */
        gMenuOpen = 0;
        RequestRefresh();
        /* 若点在开始钮则下面再处理为打开 */
    }

    if (Y >= BarY && Y < Sh) {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;

        StartBtnGeom(&Bx, &By, &Bw, &Bh);
        if (X >= Bx && X < Bx + Bw && Y >= By && Y < By + Bh) {
            gMenuOpen = !gMenuOpen;
            if (gMenuOpen) {
                RebuildStartMenu();
            }
            RequestRefresh();
            return 1;
        }
        /* 任务栏其它区域：吞掉点击 */
        if (gMenuOpen) {
            gMenuOpen = 0;
            RequestRefresh();
        }
        return 1;
    }
    return 0;
}

void DesktopInit(void) {
    if (gDesktopBusy) {
        DebugWrite("desktop: Init reenter ignored\n");
        return;
    }
    gDesktopBusy = 1;

    PlaceDesktopIcons();
    LoadIconLayout();

    gDeskSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    gMenuOpen = 0;
    gMenuCount = 0;
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    LoadWallpaper();
    LoadDesktopIcons();
    ToyLogGui("Boot: Desktop Ready\n");
    DebugWrite("desktop: icons+taskbar ready (TOYOS Assets or solid)\n");
    gDesktopBusy = 0;
}

/* 热切分辨率：只重算壁纸缓存与图标坐标，不重读 BMP（防 FAT/长循环重入） */
void DesktopOnDisplayResize(void) {
    if (gDesktopBusy) {
        DebugWrite("desktop: resize reenter ignored\n");
        return;
    }
    gDesktopBusy = 1;
    /* 热切：钳已存坐标，勿重置为默认竖列（PR-G-desk-1） */
    ClampAllIcons();
    gMenuOpen = 0;
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    FreeWallScreen();
    if (gWallReady) {
        BuildWallScreen();
    }
    gDesktopBusy = 0;
}

void DesktopRefreshLabels(void) {
    gIcons[0].Label = LocStr(MSG_ICON_SHELL);
    gIcons[1].Label = LocStr(MSG_ICON_SETTINGS);
    gIcons[2].Label = LocStr(MSG_ICON_FILES);
    gIcons[3].Label = LocStr(MSG_ICON_STORE);
}

void DesktopTickClock(void) {
    static UINT32 Skip;
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int Ok;

    /* 勿每帧读 CMOS；约几十次 Poll 再查一次 */
    if (++Skip < 45u) {
        return;
    }
    Skip = 0;

    Ok = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (Ok) {
        if (gClockValid && Hour == gClockHour && Minute == gClockMinute) {
            return;
        }
    } else if (!gClockValid) {
        return;
    }
    /*
     * 勿 BeginFront：UI scale≠100 时逻辑坐标直写物理 GOP →
     * 屏幕中部出现「更细」假任务栏，鼠标 Present 像橡皮擦掉。
     * 走后缓冲 + Present（含缩放）与桌面其它绘制一致。
     */
    DrawTaskbarRaw();
    if (gMenuOpen) {
        DrawStartMenuRaw();
    }
    HalVideoPresent();
}

int DesktopHandleClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction,
                       char *OutExecPath, UINTN ExecPathMax) {
    int i;
    int Hit;
    int Prev;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    if (OutAction) {
        *OutAction = DESKTOP_ACTION_NONE;
    }
    if (OutExecPath && ExecPathMax > 0) {
        OutExecPath[0] = 0;
    }

    if (HandleTaskbarClick(X, Y, OutAction, OutExecPath, ExecPathMax)) {
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        return 1;
    }

    Hit = -1;
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (PointInIcon(&gIcons[i], X, Y)) {
            Hit = i;
            break;
        }
    }

    Now = DesktopClock();
    if (Hit < 0) {
        Prev = gDeskSelected;
        gDeskSelected = -1;
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        if (Prev >= 0) {
            RedrawIconIndex(Prev);
        }
        return 0;
    }

    Dt = (Now >= gSelectClock) ? (Now - gSelectClock) : DESKTOP_DBLCLICK_MAX + 1;
    Dx = (X >= gSelectX) ? (X - gSelectX) : (gSelectX - X);
    Dy = (Y >= gSelectY) ? (Y - gSelectY) : (gSelectY - Y);

    if (Hit == gDeskSelected &&
        Dt <= DESKTOP_DBLCLICK_MAX &&
        Dx <= DESKTOP_DBLCLICK_SLOP &&
        Dy <= DESKTOP_DBLCLICK_SLOP) {
        gMenuOpen = 0;
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        if (OutAction) {
            *OutAction = gIcons[Hit].Action;
        }
        gDeskSelected = -1;
        return 1;
    }

    SelectIcon(Hit, X, Y, Now);
    /* PR-G-desk-1：武装拖放；位移超阈值才真正移动 */
    gIconDragIdx = Hit;
    gIconDragOffX = (INT32)X - (INT32)gIcons[Hit].X;
    gIconDragOffY = (INT32)Y - (INT32)gIcons[Hit].Y;
    gIconDragStartX = X;
    gIconDragStartY = Y;
    gIconDragMoved = 0;
    return 1;
}
