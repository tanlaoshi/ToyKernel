/*
 * DesktopGeom.c — 图标 / 任务栏 / 菜单几何
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

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
    UINT32 LabelX;
    UINT32 Right;
    UINT32 Bottom;

    LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
    LabelX = Icon->X;
    if (LabelW > 0 && LabelW < DESKTOP_ICON_SIZE) {
        LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
    }
    Right = Icon->X + DESKTOP_ICON_SIZE;
    if (LabelW > 0 && LabelX + LabelW > Right) {
        Right = LabelX + LabelW;
    }
    Bottom = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH();
    *X = (LabelW > 0 && LabelX < Icon->X) ? LabelX : Icon->X;
    *Y = Icon->Y;
    *W = Right - *X;
    *H = Bottom - Icon->Y;
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

/* 相对 DESKTOP_ORIGIN 按图标格吸附（拖放松手 / 读盘后） */
void SnapIconToGrid(UINT32 *X, UINT32 *Y) {
    UINT32 CellW;
    UINT32 CellH;
    UINT32 RelX;
    UINT32 RelY;
    UINT32 Col;
    UINT32 Row;

    if (!X || !Y) {
        return;
    }
    CellW = DESKTOP_ICON_SIZE + DESKTOP_ICON_GAP;
    CellH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() + DESKTOP_ICON_GAP;
    if (CellW == 0 || CellH == 0) {
        return;
    }
    RelX = (*X > DESKTOP_ORIGIN_X) ? (*X - DESKTOP_ORIGIN_X) : 0;
    RelY = (*Y > DESKTOP_ORIGIN_Y) ? (*Y - DESKTOP_ORIGIN_Y) : 0;
    Col = (RelX + CellW / 2u) / CellW;
    Row = (RelY + CellH / 2u) / CellH;
    *X = DESKTOP_ORIGIN_X + Col * CellW;
    *Y = DESKTOP_ORIGIN_Y + Row * CellH;
    ClampIconPos(X, Y);
}

void ClampAllIcons(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        SnapIconToGrid(&gIcons[i].X, &gIcons[i].Y);
    }
}

void AppsFlyoutGeom(UINT32 *Fx, UINT32 *Fy, UINT32 *Fw, UINT32 *Fh) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    int AppsIdx = -1;
    int i;
    int Rows;

    if (!Fx || !Fy || !Fw || !Fh) {
        return;
    }
    TaskbarGeom(&BarY, &Sw, &Sh);
    MenuGeom(&Mx, &My, &Mw, &Mh);
    for (i = 0; i < gMenuCount; i++) {
        if (gMenuRows[i].Action == DESKTOP_ACTION_APPS) {
            AppsIdx = i;
            break;
        }
    }
    if (AppsIdx < 0) {
        AppsIdx = MENU_FIXED_TOP - 1;
    }
    Rows = gMenuAppCount > 0 ? gMenuAppCount : 1;
    *Fw = MENU_W;
    if (*Fw + 8u > Sw) {
        *Fw = Sw > 8u ? Sw - 8u : Sw;
    }
    *Fh = MENU_ITEM_H * (UINT32)Rows;
    *Fx = Mx + Mw;
    if (*Fx + *Fw > Sw && Mw + 4u < Sw) {
        /* 右侧放不下则叠在主菜单右侧内缩 */
        *Fx = (Sw > *Fw + 4u) ? (Sw - *Fw - 4u) : 0;
    }
    *Fy = My + (UINT32)AppsIdx * MENU_ITEM_H;
    if (*Fy + *Fh > BarY && *Fh <= BarY) {
        *Fy = BarY - *Fh;
    }
    if (*Fy + *Fh > BarY) {
        *Fh = (BarY > *Fy) ? (BarY - *Fy) : MENU_ITEM_H;
    }
}
