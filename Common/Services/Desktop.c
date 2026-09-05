/*
 * Desktop.c — 桌面图标 + 任务栏/开始菜单 + BMP 壁纸（PR-D4 / PR-G13）
 *
 * 开窗：桌面双击图标，或任务栏「开始」菜单（不单靠图标）。
 * 壁纸：根目录 WALL.BMP（BI_RGB）；缺失则 ThemeDesktopBg 纯色。
 */
#include "Desktop.h"
#include "Gui.h"
#include "Console.h"
#include "UI.h"
#include "Hal.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Bmp.h"
#include "FileSystem.h"
#include "PhysicalMemory.h"
#include "Debug.h"

#define DESKTOP_ICON_COUNT    3
#define DESKTOP_ICON_SIZE     48
#define DESKTOP_ICON_GAP      28
#define DESKTOP_ORIGIN_X      36
#define DESKTOP_ORIGIN_Y      36
#define DESKTOP_LABEL_PAD     6
#define DESKTOP_DBLCLICK_SLOP 16u
#define DESKTOP_DBLCLICK_MAX  2000000ULL

#define TASKBAR_H             32u
#define START_BTN_PAD_X       12u
#define START_BTN_MIN_W       56u
#define MENU_W                148u
#define MENU_ITEM_H           28u
#define MENU_ITEMS            3
#define WALL_FILE_MAX         (512u * 1024u)

typedef enum {
    DESKTOP_ACT_SHELL = 0,
    DESKTOP_ACT_SETTINGS,
    DESKTOP_ACT_FILES
} DESKTOP_ACTION;

typedef struct {
    const char     *Label;
    DESKTOP_ACTION  Action;
    UINT32          IconColor;
    UINT32          X;
    UINT32          Y;
} DESKTOP_ICON;

static DESKTOP_ICON gIcons[DESKTOP_ICON_COUNT];
static int gSelected = -1;
static UINT64 gSelectClock;
static UINT32 gSelectX;
static UINT32 gSelectY;

static BMP_IMAGE gWall;
static int gWallReady;
static int gMenuOpen;

/* 已按当前分辨率拉伸的壁纸缓存（加速 DesktopFillRect，避免拖死鼠标） */
static UINT32 *gWallScreen;
static UINT32  gWallScreenW;
static UINT32  gWallScreenH;
static UINT32  gWallScreenPages;

static UINT64 DesktopClock(void) {
    return HalCpuTicks(0);
}

static int RectsOverlap(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                        UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Ax + Aw > Bx && Ay < By + Bh && Ay + Ah > By;
}

static void IconBounds(const DESKTOP_ICON *Icon, UINT32 *X, UINT32 *Y,
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

static int PointInIcon(const DESKTOP_ICON *Icon, UINT32 X, UINT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;

    IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
    return X >= Ix && X < Ix + Iw && Y >= Iy && Y < Iy + Ih;
}

static void TaskbarGeom(UINT32 *BarY, UINT32 *Sw, UINT32 *Sh) {
    HalVideoGetSize(Sw, Sh);
    *BarY = (*Sh > TASKBAR_H) ? (*Sh - TASKBAR_H) : 0;
}

/* 开始钮宽度随字体/文案变化，避免 “Start” 画出灰底 */
static void StartBtnGeom(UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Tw;
    UINT32 Bh;
    const char *Start;

    TaskbarGeom(&BarY, &Sw, &Sh);
    Start = LocStr(MSG_START);
    Tw = FontStringWidth(Start ? Start : "Start");
    *OutW = Tw + START_BTN_PAD_X * 2;
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

static void MenuGeom(UINT32 *Mx, UINT32 *My, UINT32 *Mw, UINT32 *Mh) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;

    TaskbarGeom(&BarY, &Sw, &Sh);
    *Mw = MENU_W;
    *Mh = MENU_ITEM_H * MENU_ITEMS;
    *Mx = 4;
    *My = (BarY > *Mh) ? (BarY - *Mh) : 0;
}

static void FreeWallScreen(void) {
    if (gWallScreen && gWallScreenPages) {
        PhysicalMemoryFreePages(gWallScreen, gWallScreenPages);
    }
    gWallScreen = 0;
    gWallScreenW = 0;
    gWallScreenH = 0;
    gWallScreenPages = 0;
}

static void BuildWallScreen(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 Y;
    UINT32 X;

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0 || Sh == 0) {
        return;
    }
    if (gWallScreen && gWallScreenW == Sw && gWallScreenH == Sh) {
        return;
    }
    FreeWallScreen();
    if (!gWallReady || !gWall.Pixels || gWall.Width == 0 || gWall.Height == 0) {
        return;
    }
    Bytes = (UINT64)Sw * (UINT64)Sh * sizeof(UINT32);
    Pages = (UINT32)((Bytes + 4095ull) / 4096ull);
    if (Pages == 0) {
        return;
    }
    gWallScreen = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!gWallScreen) {
        return;
    }
    gWallScreenPages = Pages;
    gWallScreenW = Sw;
    gWallScreenH = Sh;
    for (Y = 0; Y < Sh; Y++) {
        UINT32 Sy = (Y * gWall.Height) / Sh;
        if (Sy >= gWall.Height) {
            Sy = gWall.Height - 1;
        }
        for (X = 0; X < Sw; X++) {
            UINT32 Sx = (X * gWall.Width) / Sw;
            if (Sx >= gWall.Width) {
                Sx = gWall.Width - 1;
            }
            gWallScreen[Y * Sw + X] = gWall.Pixels[Sy * gWall.Width + Sx];
        }
    }
}

static void LoadWallpaper(void) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;

    BmpFree(&gWall);
    FreeWallScreen();
    gWallReady = 0;
    Pages = (WALL_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        DebugWrite("desktop: wallpaper alloc failed\n");
        return;
    }
    Size = 0;
    Err = FsReadFile("WALL.BMP", Buf, WALL_FILE_MAX, &Size);
    if (Err != FAT_OK || Size < 54) {
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite("desktop: WALL.BMP missing; solid ThemeDesktopBg\n");
        return;
    }
    if (BmpDecode(Buf, Size, &gWall) != 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite("desktop: WALL.BMP decode failed\n");
        return;
    }
    PhysicalMemoryFreePages(Buf, Pages);
    gWallReady = 1;
    BuildWallScreen();
    DebugWrite("desktop: wallpaper WALL.BMP loaded\n");
}

UINT32 DesktopBgAt(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;

    BuildWallScreen();
    if (gWallScreen && gWallScreenW && gWallScreenH) {
        if (X >= gWallScreenW) {
            X = gWallScreenW - 1;
        }
        if (Y >= gWallScreenH) {
            Y = gWallScreenH - 1;
        }
        return gWallScreen[Y * gWallScreenW + X];
    }
    HalVideoGetSize(&Sw, &Sh);
    (void)Sw;
    (void)Sh;
    return ThemeDesktopBg();
}

void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Row;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 CopyW;

    if (W == 0 || H == 0) {
        return;
    }
    BuildWallScreen();
    if (!gWallScreen) {
        UiFillRectangle(X, Y, W, H, ThemeDesktopBg());
        return;
    }
    Sw = gWallScreenW;
    Sh = gWallScreenH;
    if (X >= Sw || Y >= Sh) {
        return;
    }
    if (X + W > Sw) {
        W = Sw - X;
    }
    if (Y + H > Sh) {
        H = Sh - Y;
    }
    CopyW = W;
    for (Row = 0; Row < H; Row++) {
        HalVideoWriteRect(X, Y + Row, CopyW, 1,
                          &gWallScreen[(Y + Row) * Sw + X]);
    }
}

/* 只画不被窗口盖住的像素 */
static void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color) {
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
            int Free = !GuiPointInAnyWindow(X + Col, Y + Row);
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

static void DrawStringFree(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cx = X;

    if (!Text) {
        return;
    }
    while (*Text) {
        UINT32 Cp;
        UINTN N;
        UINT32 Adv;
        char One[5];
        UINTN k;

        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        Adv = FontCodepointAdvance(Cp);
        for (k = 0; k < N && k < sizeof(One) - 1; k++) {
            One[k] = Text[k];
        }
        One[k] = 0;
        if (!GuiPointInAnyWindow(Cx, Y)) {
            HalVideoDrawStringAt(Cx, Y, One, Color);
        }
        Cx += Adv;
        Text += N;
    }
}

static void DrawOneIconRaw(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    UiFillRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Icon->IconColor);
    Border = Selected ? COLOR_YELLOW : COLOR_WHITE;
    UiDrawRectangle(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                    Border);
    if (Selected) {
        UiDrawRectangle(Icon->X + 1, Icon->Y + 1,
                        DESKTOP_ICON_SIZE - 2, DESKTOP_ICON_SIZE - 2,
                        COLOR_YELLOW);
    }
    UiFillRectangle(Icon->X + DESKTOP_ICON_SIZE - 14, Icon->Y,
                    14, 14, COLOR_LIGHT_GRAY);

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

static void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected) {
    UINT32 LabelX;
    UINT32 LabelY;
    UINT32 LabelW;
    UINT32 Border;

    FillRectFree(Icon->X, Icon->Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                 Icon->IconColor);
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
    FillRectFree(Icon->X + DESKTOP_ICON_SIZE - 14, Icon->Y, 14, 14,
                 COLOR_LIGHT_GRAY);

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

static void DrawTaskbarRaw(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Tx;
    UINT32 Ty;
    UINT32 Tw;
    const char *Start;

    TaskbarGeom(&BarY, &Sw, &Sh);
    StartBtnGeom(&Bx, &By, &Bw, &Bh);
    Start = LocStr(MSG_START);
    Tw = FontStringWidth(Start ? Start : "Start");

    UiFillRectangle(0, BarY, Sw, TASKBAR_H, COLOR_DARK_GRAY);
    UiDrawRectangle(0, BarY, Sw, TASKBAR_H, COLOR_GRAY);
    UiFillRectangle(Bx, By, Bw, Bh, gMenuOpen ? COLOR_BLUE : COLOR_LIGHT_GRAY);
    UiDrawRectangle(Bx, By, Bw, Bh, COLOR_WHITE);
    Tx = Bx + (Bw > Tw ? (Bw - Tw) / 2 : 0);
    Ty = BarY + (TASKBAR_H > FontCellH() ? (TASKBAR_H - FontCellH()) / 2 : 0);
    HalVideoDrawStringAt(Tx, Ty, Start ? Start : "Start",
                         gMenuOpen ? COLOR_WHITE : COLOR_BLACK);
}

static void DrawStartMenuRaw(void) {
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int i;
    const char *Labels[MENU_ITEMS];

    if (!gMenuOpen) {
        return;
    }
    MenuGeom(&Mx, &My, &Mw, &Mh);
    Labels[0] = LocStr(MSG_ICON_SHELL);
    Labels[1] = LocStr(MSG_ICON_SETTINGS);
    Labels[2] = LocStr(MSG_ICON_FILES);
    UiFillRectangle(Mx, My, Mw, Mh, COLOR_LIGHT_GRAY);
    UiDrawRectangle(Mx, My, Mw, Mh, COLOR_BLACK);
    for (i = 0; i < MENU_ITEMS; i++) {
        UINT32 Iy = My + (UINT32)i * MENU_ITEM_H;
        UiDrawRectangle(Mx, Iy, Mw, MENU_ITEM_H, COLOR_GRAY);
        HalVideoDrawStringAt(Mx + 10, Iy + (MENU_ITEM_H - FontCellH()) / 2,
                             Labels[i], COLOR_BLACK);
    }
}

static void DrawTaskbarOccluded(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;

    TaskbarGeom(&BarY, &Sw, &Sh);
    FillRectFree(0, BarY, Sw, TASKBAR_H, COLOR_DARK_GRAY);
    /* 开始钮与字：用 raw 再画一遍；遮挡复杂时略糙可接受 */
    DrawTaskbarRaw();
    if (gMenuOpen) {
        DrawStartMenuRaw();
    }
}

void DesktopDraw(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        DrawOneIconRaw(&gIcons[i], i == gSelected);
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
            DrawOneIconOccluded(&gIcons[i], i == gSelected);
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
    UINT32 Scale;
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
    Scale = (Face && Face->Scale) ? Face->Scale : 1u;

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) {
            *Out = COLOR_LIGHT_GRAY;
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
            *Out = gMenuOpen ? COLOR_BLUE : COLOR_LIGHT_GRAY;
            return 1;
        }
        *Out = COLOR_DARK_GRAY;
        return 1;
    }

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const DESKTOP_ICON *Icon = &gIcons[i];
        int Selected = (i == gSelected);
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
            if (X >= Icon->X + DESKTOP_ICON_SIZE - 14 && Y >= Icon->Y &&
                X < Icon->X + DESKTOP_ICON_SIZE && Y < Icon->Y + 14) {
                *Out = COLOR_LIGHT_GRAY;
                return 1;
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
                    UINT32 OffY = 0;
                    UINT32 Bpr = (Gw + 7) / 8;

                    if (CellH > Gh * Scale) {
                        OffY = (CellH - Gh * Scale) / 2;
                    }
                    if (RelY >= OffY) {
                        Gx = RelX / Scale;
                        Gy = (RelY - OffY) / Scale;
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

static void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    DrawOneIconOccluded(&gIcons[Idx], Idx == gSelected);
}

static void OpenAction(DESKTOP_ACTION Action) {
    int Idx;

    gMenuOpen = 0;
    if (Action == DESKTOP_ACT_SHELL) {
        Idx = GuiOpenShell();
        if (Idx >= 0) {
            ConsoleOnShellOpened();
        }
        return;
    }
    if (Action == DESKTOP_ACT_SETTINGS) {
        (void)GuiOpenSettings();
        return;
    }
    if (Action == DESKTOP_ACT_FILES) {
        (void)GuiOpenFiles();
    }
}

static void SelectIcon(int Hit, UINT32 X, UINT32 Y, UINT64 Now) {
    int Prev = gSelected;

    gSelected = Hit;
    gSelectClock = Now;
    gSelectX = X;
    gSelectY = Y;
    if (Prev >= 0 && Prev != Hit) {
        RedrawIconIndex(Prev);
    }
    RedrawIconIndex(Hit);
}

static int HandleTaskbarClick(UINT32 X, UINT32 Y) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    int Item;

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) {
            Item = (int)((Y - My) / MENU_ITEM_H);
            if (Item >= 0 && Item < MENU_ITEMS) {
                /* 先关菜单并刷新桌面，再开窗——禁止在 Open 后再全屏 Fill（会抹掉 Shell） */
                gMenuOpen = 0;
                GuiRefreshDesktop();
                OpenAction((DESKTOP_ACTION)Item);
                return 1;
            }
        }
        /* 点在菜单外：关菜单并刷新 */
        gMenuOpen = 0;
        GuiRefreshDesktop();
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
            GuiRefreshDesktop();
            return 1;
        }
        /* 任务栏其它区域：吞掉点击 */
        if (gMenuOpen) {
            gMenuOpen = 0;
            GuiRefreshDesktop();
        }
        return 1;
    }
    return 0;
}

void DesktopInit(void) {
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;

    gIcons[0].Action = DESKTOP_ACT_SHELL;
    gIcons[0].IconColor = COLOR_BLUE;
    gIcons[0].X = DESKTOP_ORIGIN_X;
    gIcons[0].Y = DESKTOP_ORIGIN_Y;

    gIcons[1].Action = DESKTOP_ACT_SETTINGS;
    gIcons[1].IconColor = 0x00606080;
    gIcons[1].X = DESKTOP_ORIGIN_X;
    gIcons[1].Y = DESKTOP_ORIGIN_Y + RowH;

    gIcons[2].Action = DESKTOP_ACT_FILES;
    gIcons[2].IconColor = 0x00208040;
    gIcons[2].X = DESKTOP_ORIGIN_X;
    gIcons[2].Y = DESKTOP_ORIGIN_Y + RowH * 2;

    DesktopRefreshLabels();

    gSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    gMenuOpen = 0;
    LoadWallpaper();
    DebugWrite("desktop: icons+taskbar ready (PR-G13)\n");
}

void DesktopRefreshLabels(void) {
    gIcons[0].Label = LocStr(MSG_ICON_SHELL);
    gIcons[1].Label = LocStr(MSG_ICON_SETTINGS);
    gIcons[2].Label = LocStr(MSG_ICON_FILES);
}

int DesktopHandleClick(UINT32 X, UINT32 Y) {
    int i;
    int Hit;
    int Prev;
    UINT64 Now;
    UINT64 Dt;
    UINT32 Dx;
    UINT32 Dy;

    if (HandleTaskbarClick(X, Y)) {
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
        Prev = gSelected;
        gSelected = -1;
        if (Prev >= 0) {
            RedrawIconIndex(Prev);
        }
        return 0;
    }

    Dt = (Now >= gSelectClock) ? (Now - gSelectClock) : DESKTOP_DBLCLICK_MAX + 1;
    Dx = (X >= gSelectX) ? (X - gSelectX) : (gSelectX - X);
    Dy = (Y >= gSelectY) ? (Y - gSelectY) : (gSelectY - Y);

    if (Hit == gSelected &&
        Dt <= DESKTOP_DBLCLICK_MAX &&
        Dx <= DESKTOP_DBLCLICK_SLOP &&
        Dy <= DESKTOP_DBLCLICK_SLOP) {
        OpenAction(gIcons[Hit].Action);
        gSelected = -1;
        return 1;
    }

    SelectIcon(Hit, X, Y, Now);
    return 1;
}
