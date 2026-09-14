/*
 * Desktop.c — 桌面图标 + 任务栏/开始菜单 + BMP 壁纸/图标（PR-D4 / PR-G13）
 *
 * 开窗：桌面双击图标，或任务栏「开始」菜单（不单靠图标）。
 * PR-G-desk-1：图标可拖放；松手写入 TOYOS.DB（ic0..ic3=x,y）；启动时 LoadIconLayout。
 * PR-G-desk-2：开始菜单动态列出 Apps/ 下 .ELF + 缺文件 INST(app) 灰显；点选 ProcessExec。
 * 壁纸：Assets/Images/WALL.BMP；图标：Assets/Icons/bmp48/SHELL|SET|FILES|STORE|START|POWER|REBOOT.BMP。
 * 四个桌面图标另有内核内置 48×48 回退（真机缺文件也能显示）。
 * 均为 BI_RGB，运行时 FileSystemReadFile + BmpDecode；缺失则回退色块。
 * 默认 FAT 无 Assets 时 FileSystemReadFile 回退 RES: 内嵌副本（真机无 USB TOYOS）。
 */
#include "Desktop.h"
#include "UI.h"
#include "Hal.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "Bmp.h"
#include "Db.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Store.h"
#include "PhysicalMemory.h"
#include "Debug.h"
#include "ToySerialLog.h"

#define DESKTOP_ICON_COUNT    4
#define DESKTOP_ICON_SIZE     48
#define DESKTOP_ICON_GAP      28
#define DESKTOP_ORIGIN_X      36
#define DESKTOP_ORIGIN_Y      36
#define DESKTOP_LABEL_PAD     6
#define DESKTOP_DBLCLICK_SLOP 16u
#define DESKTOP_DBLCLICK_MAX  2000000ULL
#define DESKTOP_DRAG_THRESH   6u /* 超过此像素才算拖放，避免误伤双击 */

#define TASKBAR_H             32u
#define START_BTN_PAD_X       8u
#define START_BTN_MIN_W       56u
#define START_ICON_SZ         20u
#define MENU_W                200u
#define MENU_ITEM_H           28u
#define MENU_ICON_SZ          18u
#define MENU_FIXED_TOP        4   /* Shell/Settings/Files/Store */
#define MENU_FIXED_BOT        2   /* Shutdown/Reboot */
#define MENU_APP_MAX          16
#define MENU_ROWS_MAX         (MENU_FIXED_TOP + MENU_APP_MAX + MENU_FIXED_BOT)
#define MENU_LABEL_MAX        40
#define MENU_PATH_MAX         80
#define WALL_FILE_MAX         (512u * 1024u)
#define ICON_FILE_MAX         (16u * 1024u)

/* 开始菜单行（PR-G-desk-2：固定项 + Apps/ 下 .ELF + 缺文件 INST 灰显） */
typedef struct {
    DESKTOP_ACTION Action;
    char           Label[MENU_LABEL_MAX];
    char           Path[MENU_PATH_MAX]; /* EXEC：Apps/FOO.ELF */
    int            Enabled;             /* 0=灰显不可点 */
    int            IconSrc;             /* 0..3 桌面图；4 关机；5 重启；-1 通用 */
} MENU_ROW;

static MENU_ROW gMenuRows[MENU_ROWS_MAX];
static int gMenuCount;
static FAT_DIRECTORY_ENTRY gMenuDirScratch[FAT_LIST_MAX];
static STORE_INSTALLED gMenuInstScratch[STORE_INSTALLED_MAX];

typedef struct {
    const char     *Label;
    DESKTOP_ACTION  Action;
    UINT32          IconColor;
    const char     *BmpPath;
    BMP_IMAGE       Bmp;
    int             BmpReady;
    UINT32          X;
    UINT32          Y;
} DESKTOP_ICON;

static DESKTOP_ICON gIcons[DESKTOP_ICON_COUNT];
static int gSelected = -1;
static UINT64 gSelectClock;
static UINT32 gSelectX;
static UINT32 gSelectY;

/* PR-G-desk-1：图标拖放状态 */
static int gIconDragIdx = -1;
static INT32 gIconDragOffX;
static INT32 gIconDragOffY;
static UINT32 gIconDragStartX;
static UINT32 gIconDragStartY;
static int gIconDragMoved;

static BMP_IMAGE gWall;
static int gWallReady;
static BMP_IMAGE gStartBmp;
static int gStartBmpReady;
static BMP_IMAGE gPowerBmp;
static int gPowerBmpReady;
static BMP_IMAGE gRebootBmp;
static int gRebootBmpReady;
static int gMenuOpen;
static UINT8 gClockHour;
static UINT8 gClockMinute;
static int gClockValid;

/* 已按当前分辨率拉伸的壁纸缓存（加速 DesktopFillRect，避免拖死鼠标） */
static UINT32 *gWallScreen;
static UINT32  gWallScreenW;
static UINT32  gWallScreenH;
static UINT32  gWallScreenPages;
static int     gDesktopBusy; /* 防 DesktopInit / OnDisplayResize 重入 */

/* PR-R2：由 Gui 注册，Desktop 不 include Gui.h */
static int (*gPointOccupied)(UINT32 X, UINT32 Y);
static void (*gRequestRefresh)(void);

static void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);
static void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected);
static void RedrawIconIndex(int Idx);

static int PointOccupied(UINT32 X, UINT32 Y) {
    return gPointOccupied ? gPointOccupied(X, Y) : 0;
}

static void RequestRefresh(void) {
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

/* 开始钮：可选 START.BMP + 文案；宽度随字体变化 */
static void StartBtnGeom(UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH) {
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
    IconSlot = gStartBmpReady ? (START_ICON_SZ + 6u) : 0;
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

/* 读 FAT 上 BI_RGB BMP 到 Out；成功返回 1 */
static int LoadBmpPath(const char *Path, BMP_IMAGE *Out, UINT32 FileMax,
                       const char *Tag) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;

    (void)Tag;
    if (!Path || !Out || FileMax < 54) {
        return 0;
    }
    BmpFree(Out);
    Pages = (FileMax + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        DebugWrite(Tag);
        DebugWrite(": alloc failed\n");
        return 0;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, FileMax, &Size);
    if (Err != FAT_OK || Size < 54) {
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite(Tag);
        DebugWrite(": missing ");
        DebugWrite(Path);
        DebugWrite("\n");
        return 0;
    }
    if (BmpDecode(Buf, Size, Out) != 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        DebugWrite(Tag);
        DebugWrite(": decode failed ");
        DebugWrite(Path);
        DebugWrite("\n");
        return 0;
    }
    PhysicalMemoryFreePages(Buf, Pages);
    DebugWrite(Tag);
    DebugWrite(": loaded ");
    DebugWrite(Path);
    DebugWrite("\n");
    return 1;
}

#include "IconDesktop48.inc"

static int LoadBuiltinIcon(BMP_IMAGE *Out, const UINT32 *Src, const char *Tag) {
    UINT32 Pages;
    UINT32 *Dst;
    UINT32 i;

    (void)Tag; /* TOY_DEBUG=0 时 DebugWrite 为空，避免 -Wunused-parameter */
    if (!Out || !Src) {
        return 0;
    }
    BmpFree(Out);
    Pages = (48u * 48u * sizeof(UINT32) + 4095u) / 4096u;
    Dst = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Dst) {
        DebugWrite(Tag);
        DebugWrite(": builtin alloc fail\n");
        return 0;
    }
    for (i = 0; i < 48u * 48u; i++) {
        Dst[i] = Src[i];
    }
    Out->Pixels = Dst;
    Out->Width = 48;
    Out->Height = 48;
    Out->Pages = Pages;
    DebugWrite(Tag);
    DebugWrite(": builtin ok\n");
    return 1;
}

static void LoadDesktopIcons(void) {
    int i;
    static const UINT32 *const Builtin[DESKTOP_ICON_COUNT] = {
        gIconShell48, gIconSet48, gIconFiles48, gIconStore48
    };
    static const char *const BuiltinTag[DESKTOP_ICON_COUNT] = {
        "desktop: shell", "desktop: set", "desktop: files", "desktop: store"
    };

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        gIcons[i].BmpReady = 0;
        if (!gIcons[i].BmpPath) {
            continue;
        }
        gIcons[i].BmpReady = LoadBmpPath(gIcons[i].BmpPath, &gIcons[i].Bmp,
                                         ICON_FILE_MAX, BuiltinTag[i]);
        if (!gIcons[i].BmpReady) {
            gIcons[i].BmpReady =
                LoadBuiltinIcon(&gIcons[i].Bmp, Builtin[i], BuiltinTag[i]);
        }
    }
    gStartBmpReady = LoadBmpPath("Assets/Icons/bmp48/START.BMP", &gStartBmp,
                                 ICON_FILE_MAX, "desktop: start");
    gPowerBmpReady = LoadBmpPath("Assets/Icons/bmp48/POWER.BMP", &gPowerBmp,
                                 ICON_FILE_MAX, "desktop: power");
    if (!gPowerBmpReady) {
        gPowerBmpReady = LoadBuiltinIcon(&gPowerBmp, gIconPower48, "desktop: power");
    }
    gRebootBmpReady = LoadBmpPath("Assets/Icons/bmp48/REBOOT.BMP", &gRebootBmp,
                                  ICON_FILE_MAX, "desktop: reboot");
    if (!gRebootBmpReady) {
        gRebootBmpReady =
            LoadBuiltinIcon(&gRebootBmp, gIconReboot48, "desktop: reboot");
    }
}

static UINT32 BmpSampleScaled(const BMP_IMAGE *Img, UINT32 Dx, UINT32 Dy,
                              UINT32 Dw, UINT32 Dh) {
    UINT32 Sx;
    UINT32 Sy;

    if (!Img || !Img->Pixels || Img->Width == 0 || Img->Height == 0 ||
        Dw == 0 || Dh == 0) {
        return 0;
    }
    Sx = (Dx * Img->Width) / Dw;
    Sy = (Dy * Img->Height) / Dh;
    if (Sx >= Img->Width) {
        Sx = Img->Width - 1;
    }
    if (Sy >= Img->Height) {
        Sy = Img->Height - 1;
    }
    return Img->Pixels[Sy * Img->Width + Sx];
}

static void BlitBmpScaledRaw(UINT32 X, UINT32 Y, UINT32 Dw, UINT32 Dh,
                             const BMP_IMAGE *Img) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Line[64];

    if (!Img || !Img->Pixels || Dw == 0 || Dh == 0) {
        return;
    }
    if (Dw > 64) {
        Dw = 64;
    }
    for (Row = 0; Row < Dh; Row++) {
        for (Col = 0; Col < Dw; Col++) {
            Line[Col] = BmpSampleScaled(Img, Col, Row, Dw, Dh);
        }
        HalVideoWriteRect(X, Y + Row, Dw, 1, Line);
    }
}

/* BlitBmpScaledFree reserved if taskbar occlusion needs per-pixel later */

static void BlitIconFaceRaw(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    UINT32 W;
    UINT32 H;
    UINT32 Row;

    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        W = Icon->Bmp.Width;
        H = Icon->Bmp.Height;
        if (W > DESKTOP_ICON_SIZE) {
            W = DESKTOP_ICON_SIZE;
        }
        if (H > DESKTOP_ICON_SIZE) {
            H = DESKTOP_ICON_SIZE;
        }
        for (Row = 0; Row < H; Row++) {
            HalVideoWriteRect(X, Y + Row, W, 1,
                              &Icon->Bmp.Pixels[Row * Icon->Bmp.Width]);
        }
        return;
    }
    UiFillRectangle(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Icon->IconColor);
}

static void BlitIconFaceFree(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon) {
    UINT32 W;
    UINT32 H;
    UINT32 Row;
    UINT32 Col;
    UINT32 RunStart;
    int InRun;

    if (Icon->BmpReady && Icon->Bmp.Pixels) {
        W = Icon->Bmp.Width;
        H = Icon->Bmp.Height;
        if (W > DESKTOP_ICON_SIZE) {
            W = DESKTOP_ICON_SIZE;
        }
        if (H > DESKTOP_ICON_SIZE) {
            H = DESKTOP_ICON_SIZE;
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
                    HalVideoWriteRect(X + RunStart, Y + Row, Col - RunStart, 1,
                                      &Icon->Bmp.Pixels[Row * Icon->Bmp.Width +
                                                        RunStart]);
                    InRun = 0;
                }
            }
            if (InRun) {
                HalVideoWriteRect(X + RunStart, Y + Row, W - RunStart, 1,
                                  &Icon->Bmp.Pixels[Row * Icon->Bmp.Width +
                                                    RunStart]);
            }
        }
        return;
    }
    FillRectFree(X, Y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, Icon->IconColor);
}

static void MenuCopyStr(char *Dst, int Max, const char *Src) {
    int i;

    if (!Dst || Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int MenuNameEqIgnoreCase(const char *A, const char *B) {
    char Ca;
    char Cb;

    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        Ca = *A;
        Cb = *B;
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int MenuEndsWithElf(const char *Name) {
    int N = 0;

    if (!Name) {
        return 0;
    }
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return MenuNameEqIgnoreCase(Name + N - 4, ".elf");
}

static void MenuLabelFromElf(const char *File, char *Out, int Max) {
    int i;
    int N = 0;

    if (!Out || Max <= 0) {
        return;
    }
    if (!File) {
        Out[0] = 0;
        return;
    }
    while (File[N]) {
        N++;
    }
    if (N >= 4 && MenuEndsWithElf(File)) {
        N -= 4;
    }
    for (i = 0; i < N && i < Max - 1; i++) {
        Out[i] = File[i];
    }
    Out[i] = 0;
}

static int MenuPathExists(const char *Path) {
    FAT_FILE_STAT St;

    if (!Path || !Path[0]) {
        return 0;
    }
    return FileSystemFileStat(Path, &St) == 0;
}

static int MenuAlreadyHasPath(const char *Path) {
    int i;

    for (i = 0; i < gMenuCount; i++) {
        if (gMenuRows[i].Action == DESKTOP_ACTION_EXEC &&
            MenuNameEqIgnoreCase(gMenuRows[i].Path, Path)) {
            return 1;
        }
    }
    return 0;
}

static int MenuMaxAppSlots(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Room;
    int MaxRows;
    int Apps;

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)Sw;
    Room = BarY > 8u ? (BarY - 8u) : 0;
    MaxRows = (int)(Room / MENU_ITEM_H);
    if (MaxRows < MENU_FIXED_TOP + MENU_FIXED_BOT) {
        MaxRows = MENU_FIXED_TOP + MENU_FIXED_BOT;
    }
    if (MaxRows > MENU_ROWS_MAX) {
        MaxRows = MENU_ROWS_MAX;
    }
    Apps = MaxRows - MENU_FIXED_TOP - MENU_FIXED_BOT;
    if (Apps < 0) {
        Apps = 0;
    }
    if (Apps > MENU_APP_MAX) {
        Apps = MENU_APP_MAX;
    }
    return Apps;
}

static void MenuAddRow(DESKTOP_ACTION Act, const char *Label, const char *Path,
                       int Enabled, int IconSrc) {
    MENU_ROW *R;

    if (gMenuCount >= MENU_ROWS_MAX) {
        return;
    }
    R = &gMenuRows[gMenuCount++];
    R->Action = Act;
    R->Enabled = Enabled ? 1 : 0;
    R->IconSrc = IconSrc;
    MenuCopyStr(R->Label, sizeof(R->Label), Label ? Label : "");
    MenuCopyStr(R->Path, sizeof(R->Path), Path ? Path : "");
}

static void MenuEnrichLabelFromCatalog(const char *File, char *Label, int Max) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;

    if (!File || !Label || Max <= 0) {
        return;
    }
    Tab = StoreScratchTab();
    if (!Tab) {
        return;
    }
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) < 0 || Count <= 0) {
        return;
    }
    for (i = 0; i < Count; i++) {
        if (Tab[i].Type[0] == 'a' &&
            MenuNameEqIgnoreCase(Tab[i].File, File) &&
            Tab[i].Title[0]) {
            MenuCopyStr(Label, Max, Tab[i].Title);
            return;
        }
    }
}

/* 打开开始菜单时重建：系统项 + Apps/ 下 .ELF + 缺文件的 INST(app) 灰显 */
static void RebuildStartMenu(void) {
    int AppCap;
    int AppN = 0;
    int DirN = 0;
    int InstN = 0;
    int i;
    int Err;
    char Path[MENU_PATH_MAX];
    char Label[MENU_LABEL_MAX];
    const char *L;

    gMenuCount = 0;
    AppCap = MenuMaxAppSlots();

    L = LocStr(MSG_ICON_SHELL);
    MenuAddRow(DESKTOP_ACTION_SHELL, L ? L : "Shell", 0, 1, 0);
    L = LocStr(MSG_ICON_SETTINGS);
    MenuAddRow(DESKTOP_ACTION_SETTINGS, L ? L : "Settings", 0, 1, 1);
    L = LocStr(MSG_ICON_FILES);
    MenuAddRow(DESKTOP_ACTION_FILES, L ? L : "Files", 0, 1, 2);
    L = LocStr(MSG_ICON_STORE);
    MenuAddRow(DESKTOP_ACTION_STORE, L ? L : "Store", 0, 1, 3);

    Err = FileSystemListEntries(STORE_APPS_DIR, gMenuDirScratch, FAT_LIST_MAX,
                                &DirN);
    if (Err == 0 && DirN > 0) {
        for (i = 0; i < DirN && AppN < AppCap; i++) {
            const FAT_DIRECTORY_ENTRY *E = &gMenuDirScratch[i];

            if (E->Attr & FAT_ATTR_DIR) {
                continue;
            }
            if (!MenuEndsWithElf(E->Name)) {
                continue;
            }
            MenuCopyStr(Path, sizeof(Path), STORE_APPS_DIR);
            /* Apps/ + name */
            {
                int P = 0;
                while (Path[P]) {
                    P++;
                }
                if (P + 1 < (int)sizeof(Path)) {
                    Path[P++] = '/';
                    Path[P] = 0;
                }
                MenuCopyStr(Path + P, (int)sizeof(Path) - P, E->Name);
            }
            MenuLabelFromElf(E->Name, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(E->Name, Label, sizeof(Label));
            MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
            AppN++;
        }
    }

    /* INST app 但 Apps/ 无文件 → 灰显（可看见、不可开） */
    if (StoreListInstalled(gMenuInstScratch, STORE_INSTALLED_MAX, &InstN) == 0) {
        for (i = 0; i < InstN && AppN < AppCap; i++) {
            STORE_INSTALLED *In = &gMenuInstScratch[i];

            if (!(In->Type[0] == 'a' && In->Type[1] == 'p' &&
                  In->Type[2] == 'p' && In->Type[3] == 0)) {
                continue;
            }
            if (!In->File[0]) {
                continue;
            }
            MenuCopyStr(Path, sizeof(Path), STORE_APPS_DIR);
            {
                int P = 0;
                while (Path[P]) {
                    P++;
                }
                if (P + 1 < (int)sizeof(Path)) {
                    Path[P++] = '/';
                    Path[P] = 0;
                }
                MenuCopyStr(Path + P, (int)sizeof(Path) - P, In->File);
            }
            if (MenuAlreadyHasPath(Path)) {
                continue;
            }
            if (MenuPathExists(Path)) {
                /* 已在盘上但 list 漏了：仍加一行可开 */
                MenuLabelFromElf(In->File, Label, sizeof(Label));
                MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
                MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
                AppN++;
                continue;
            }
            MenuLabelFromElf(In->File, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
            if (!Label[0]) {
                MenuCopyStr(Label, sizeof(Label), In->Id);
            }
            MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 0, -1);
            AppN++;
        }
    }

    L = LocStr(MSG_ICON_SHUTDOWN);
    MenuAddRow(DESKTOP_ACTION_SHUTDOWN, L ? L : "Shutdown", 0, 1, 4);
    L = LocStr(MSG_ICON_REBOOT);
    MenuAddRow(DESKTOP_ACTION_REBOOT, L ? L : "Reboot", 0, 1, 5);
}

static void MenuGeom(UINT32 *Mx, UINT32 *My, UINT32 *Mw, UINT32 *Mh) {
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
    UINT32 WallW;
    UINT32 WallH;
    UINT32 *WallPix;
    UINT32 *Dst;

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
    /* 快照尺寸与指针，避免缩放循环中被重入释放 */
    WallPix = gWall.Pixels;
    WallW = gWall.Width;
    WallH = gWall.Height;
    Bytes = (UINT64)Sw * (UINT64)Sh * sizeof(UINT32);
    Pages = (UINT32)((Bytes + 4095ull) / 4096ull);
    if (Pages == 0) {
        return;
    }
    Dst = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Dst) {
        return;
    }
    for (Y = 0; Y < Sh; Y++) {
        UINT32 Sy = (Y * WallH) / Sh;
        if (Sy >= WallH) {
            Sy = WallH - 1;
        }
        for (X = 0; X < Sw; X++) {
            UINT32 Sx = (X * WallW) / Sw;
            if (Sx >= WallW) {
                Sx = WallW - 1;
            }
            Dst[Y * Sw + X] = WallPix[Sy * WallW + Sx];
        }
    }
    gWallScreen = Dst;
    gWallScreenPages = Pages;
    gWallScreenW = Sw;
    gWallScreenH = Sh;
}

static void ClampIconPos(UINT32 *X, UINT32 *Y) {
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

static void ClampAllIcons(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        ClampIconPos(&gIcons[i].X, &gIcons[i].Y);
    }
}

/* 解析 "x,y"；成功返回 1 */
static int ParseIconXy(const char *S, UINT32 *OutX, UINT32 *OutY) {
    UINT32 X = 0;
    UINT32 Y = 0;
    const char *P;

    if (!S || !OutX || !OutY) {
        return 0;
    }
    P = S;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        X = X * 10u + (UINT32)(*P - '0');
        P++;
        if (X > 8192u) {
            return 0;
        }
    }
    if (*P != ',') {
        return 0;
    }
    P++;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        Y = Y * 10u + (UINT32)(*P - '0');
        P++;
        if (Y > 8192u) {
            return 0;
        }
    }
    if (*P != '\0') {
        return 0;
    }
    *OutX = X;
    *OutY = Y;
    return 1;
}

static void FormatIconXy(UINT32 X, UINT32 Y, char *Out, UINTN OutMax) {
    char Tmp[16];
    UINTN N = 0;
    UINTN i;
    UINT32 V;

    if (!Out || OutMax < 4) {
        return;
    }
    V = X;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    i = 0;
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    if (i + 1 < OutMax) {
        Out[i++] = ',';
    }
    N = 0;
    V = Y;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    Out[i] = '\0';
}

static const char *IconLayoutKey(int Idx) {
    switch (Idx) {
    case 0:
        return "ic0";
    case 1:
        return "ic1";
    case 2:
        return "ic2";
    case 3:
        return "ic3";
    default:
        return 0;
    }
}

/* PR-G-desk-1：从 TOYOS.DB 覆盖默认坐标 */
static void LoadIconLayout(void) {
    int i;
    int Any = 0;
    char Val[DB_VAL_MAX];
    UINT32 X;
    UINT32 Y;
    const char *Key;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
            continue;
        }
        if (!ParseIconXy(Val, &X, &Y)) {
            continue;
        }
        ClampIconPos(&X, &Y);
        gIcons[i].X = X;
        gIcons[i].Y = Y;
        Any = 1;
    }
    if (Any) {
        DebugWrite("desktop: icon layout from TOYOS.DB\n");
    }
}

static void SaveIconLayout(void) {
    int i;
    char Val[DB_VAL_MAX];
    const char *Key;
    int Ok = 1;

    DbBeginBatch();
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        FormatIconXy(gIcons[i].X, gIcons[i].Y, Val, sizeof(Val));
        if (DbSet(Key, Val) != DB_OK) {
            Ok = 0;
        }
    }
    if (DbEndBatch() != DB_OK) {
        Ok = 0;
    }
    if (Ok) {
        DebugWrite("desktop: icon layout saved\n");
    } else {
        DebugWrite("desktop: icon layout save failed\n");
    }
}

static void PlaceDesktopIcons(void) {
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;

    gIcons[0].Action = DESKTOP_ACTION_SHELL;
    gIcons[0].IconColor = COLOR_BLUE;
    gIcons[0].BmpPath = "Assets/Icons/bmp48/SHELL.BMP";
    gIcons[0].X = DESKTOP_ORIGIN_X;
    gIcons[0].Y = DESKTOP_ORIGIN_Y;

    gIcons[1].Action = DESKTOP_ACTION_SETTINGS;
    gIcons[1].IconColor = 0x00606080;
    gIcons[1].BmpPath = "Assets/Icons/bmp48/SET.BMP";
    gIcons[1].X = DESKTOP_ORIGIN_X;
    gIcons[1].Y = DESKTOP_ORIGIN_Y + RowH;

    gIcons[2].Action = DESKTOP_ACTION_FILES;
    gIcons[2].IconColor = 0x00208040;
    gIcons[2].BmpPath = "Assets/Icons/bmp48/FILES.BMP";
    gIcons[2].X = DESKTOP_ORIGIN_X;
    gIcons[2].Y = DESKTOP_ORIGIN_Y + RowH * 2;

    gIcons[3].Action = DESKTOP_ACTION_STORE;
    gIcons[3].IconColor = 0x002080C0;
    gIcons[3].BmpPath = "Assets/Icons/bmp48/STORE.BMP";
    gIcons[3].X = DESKTOP_ORIGIN_X;
    gIcons[3].Y = DESKTOP_ORIGIN_Y + RowH * 3;

    DesktopRefreshLabels();
}

static void MoveIconTo(int Idx, UINT32 NewX, UINT32 NewY) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    ClampIconPos(&NewX, &NewY);
    if (gIcons[Idx].X == NewX && gIcons[Idx].Y == NewY) {
        return;
    }
    IconBounds(&gIcons[Idx], &Ox, &Oy, &Ow, &Oh);
    gIcons[Idx].X = NewX;
    gIcons[Idx].Y = NewY;
    DesktopFillRect(Ox, Oy, Ow, Oh);
    DesktopDrawRect(Ox, Oy, Ow, Oh);
    DrawOneIconOccluded(&gIcons[Idx], Idx == gSelected);
    HalVideoPresent();
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

static void LoadWallpaper(void) {
    FreeWallScreen();
    gWallReady = LoadBmpPath("Assets/Images/WALL.BMP", &gWall, WALL_FILE_MAX,
                             "desktop: wallpaper");
    if (gWallReady) {
        BuildWallScreen();
    }
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
    return ThemeDesktopBackground();
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
        UiFillRectangle(X, Y, W, H, ThemeDesktopBackground());
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
        if (!PointOccupied(Cx, Y)) {
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

static void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected) {
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
    Tw = FontStringWidth(Start ? Start : "Start");

    UiFillRectangle(0, BarY, Sw, TASKBAR_H, COLOR_DARK_GRAY);
    UiDrawRectangle(0, BarY, Sw, TASKBAR_H, COLOR_GRAY);
    UiFillRectangle(Bx, By, Bw, Bh, gMenuOpen ? COLOR_BLUE : COLOR_LIGHT_GRAY);
    UiDrawRectangle(Bx, By, Bw, Bh, COLOR_WHITE);

    Ix = Bx + START_BTN_PAD_X;
    Iy = By + (Bh > START_ICON_SZ ? (Bh - START_ICON_SZ) / 2 : 0);
    if (gStartBmpReady) {
        BlitBmpScaledRaw(Ix, Iy, START_ICON_SZ, START_ICON_SZ, &gStartBmp);
        Tx = Ix + START_ICON_SZ + 6u;
    } else {
        Tx = Bx + (Bw > Tw ? (Bw - Tw) / 2 : 0);
    }
    Ty = BarY + (TASKBAR_H > FontCellH() ? (TASKBAR_H - FontCellH()) / 2 : 0);
    HalVideoDrawStringAt(Tx, Ty, Start ? Start : "Start",
                         gMenuOpen ? COLOR_WHITE : COLOR_BLACK);

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
    HalVideoDrawStringAt(ClockX, Ty, Clock, COLOR_WHITE);
}

static void DrawStartMenuRaw(void) {
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
    UiFillRectangle(Mx, My, Mw, Mh, COLOR_LIGHT_GRAY);
    UiDrawRectangle(Mx, My, Mw, Mh, COLOR_BLACK);
    for (i = 0; i < gMenuCount; i++) {
        MENU_ROW *R = &gMenuRows[i];
        UINT32 Iy = My + (UINT32)i * MENU_ITEM_H;
        UINT32 IconX;
        UINT32 IconY;
        UINT32 TextX;
        UINT32 Fg;
        int HasIcon = 0;

        UiDrawRectangle(Mx, Iy, Mw, MENU_ITEM_H, COLOR_GRAY);
        IconX = Mx + 6;
        IconY = Iy + (MENU_ITEM_H > MENU_ICON_SZ ? (MENU_ITEM_H - MENU_ICON_SZ) / 2 : 0);
        TextX = Mx + 10;
        Fg = R->Enabled ? COLOR_BLACK : COLOR_DARK_GRAY;
        if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT &&
            gIcons[R->IconSrc].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[R->IconSrc].Bmp);
            HasIcon = 1;
        } else if (R->IconSrc == 4 && gPowerBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gPowerBmp);
            HasIcon = 1;
        } else if (R->IconSrc == 5 && gRebootBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gRebootBmp);
            HasIcon = 1;
        } else if (R->Action == DESKTOP_ACTION_EXEC && gIcons[0].BmpReady) {
            /* 用户 ELF：复用 Shell 小图标 */
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[0].Bmp);
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

static void RedrawIconIndex(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    DrawOneIconOccluded(&gIcons[Idx], Idx == gSelected);
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

    ToyLogGui("boot: desktop icons\n");
    PlaceDesktopIcons();
    LoadIconLayout();

    gSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    gMenuOpen = 0;
    gMenuCount = 0;
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    ToyLogGui("boot: desktop wallpaper\n");
    LoadWallpaper();
    ToyLogGui("boot: desktop bmp icons\n");
    LoadDesktopIcons();
    ToyLogGui("boot: desktop ready\n");
    DebugWrite("desktop: icons+taskbar ready (bmp48 Assets/Icons)\n");
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
        Prev = gSelected;
        gSelected = -1;
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

    if (Hit == gSelected &&
        Dt <= DESKTOP_DBLCLICK_MAX &&
        Dx <= DESKTOP_DBLCLICK_SLOP &&
        Dy <= DESKTOP_DBLCLICK_SLOP) {
        gMenuOpen = 0;
        gIconDragIdx = -1;
        gIconDragMoved = 0;
        if (OutAction) {
            *OutAction = gIcons[Hit].Action;
        }
        gSelected = -1;
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
