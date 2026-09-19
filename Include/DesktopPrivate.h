/*
 * DesktopPrivate.h — Desktop 内部共享头（仅 Common/Services 下 *.c 使用）
 *
 * 禁止 User 程序、HAL、Common/Core 包含本文件。
 * PR-S-desktop-split-1：与 DesktopWallpaper.c 一并引入。
 * 源文件在 Common/Services/Desktop/（核心 Desktop.c）。
 */
#ifndef DESKTOP_PRIVATE_H
#define DESKTOP_PRIVATE_H

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

/* ===== 宏（从 Desktop.c 搬入；值不变） ===== */
#define DESKTOP_ICON_COUNT    4
#define DESKTOP_ICON_SIZE     48
#define DESKTOP_ICON_GAP      28
#define DESKTOP_ORIGIN_X      36
#define DESKTOP_ORIGIN_Y      36
#define DESKTOP_LABEL_PAD     6
#define DESKTOP_DBLCLICK_SLOP 16u
#define DESKTOP_DBLCLICK_MAX  2000000ULL
#define DESKTOP_DRAG_THRESH   6u

#define TASKBAR_H             32u
#define START_BTN_PAD_X       8u
#define START_BTN_MIN_W       56u
#define START_ICON_SZ         20u
#define MENU_W                200u
#define MENU_ITEM_H           28u
#define MENU_ICON_SZ          18u
#define MENU_FIXED_TOP        5 /* Shell/Settings/Files/Store/Apps */
#define MENU_FIXED_BOT        2
#define MENU_APP_MAX          16
#define MENU_ROWS_MAX         (MENU_FIXED_TOP + MENU_FIXED_BOT)
#define MENU_LABEL_MAX        40
#define MENU_PATH_MAX         80
#define WALL_FILE_MAX         (512u * 1024u)
#define ICON_FILE_MAX         (16u * 1024u)

/* ===== 类型（从 Desktop.c 搬入；布局不变） ===== */
typedef struct {
    DESKTOP_ACTION Action;
    char           Label[MENU_LABEL_MAX];
    char           Path[MENU_PATH_MAX];
    int            Enabled;
    int            IconSrc;
} MENU_ROW;

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

/* ===== 全局变量 extern（定义在 Desktop.c） ===== */
extern MENU_ROW gMenuRows[MENU_ROWS_MAX];
extern int gMenuCount;
extern MENU_ROW gMenuAppRows[MENU_APP_MAX];
extern int gMenuAppCount;
extern int gMenuAppsOpen;
extern FAT_DIRECTORY_ENTRY gMenuDirScratch[FAT_LIST_MAX];
extern STORE_INSTALLED gMenuInstScratch[STORE_INSTALLED_MAX];

extern DESKTOP_ICON gIcons[DESKTOP_ICON_COUNT];
/* 原 gSelected：与 FilesUi 全局同名冲突，跨 TU 后改为 gDeskSelected */
extern int gDeskSelected;
extern UINT64 gSelectClock;
extern UINT32 gSelectX;
extern UINT32 gSelectY;

extern int gIconDragIdx;
extern INT32 gIconDragOffX;
extern INT32 gIconDragOffY;
extern UINT32 gIconDragStartX;
extern UINT32 gIconDragStartY;
extern int gIconDragMoved;

extern BMP_IMAGE gWall;
extern int gWallReady;
extern BMP_IMAGE gStartBmp;
extern int gStartBmpReady;
extern BMP_IMAGE gPowerBmp;
extern int gPowerBmpReady;
extern BMP_IMAGE gRebootBmp;
extern int gRebootBmpReady;

extern int gMenuOpen;
extern UINT8 gClockHour;
extern UINT8 gClockMinute;
extern int gClockValid;

extern UINT32 *gWallScreen;
extern UINT32  gWallScreenW;
extern UINT32  gWallScreenH;
extern UINT32  gWallScreenPages;
extern int     gDesktopBusy;

extern int (*gPointOccupied)(UINT32 X, UINT32 Y);
extern void (*gRequestRefresh)(void);
extern void (*gClearIconFootprint)(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);

/* ===== 共享函数声明 ===== */

UINT64 DesktopClock(void);
int RectsOverlap(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                 UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh);
void IconBounds(const DESKTOP_ICON *Icon, UINT32 *X, UINT32 *Y,
                UINT32 *W, UINT32 *H);
int PointInIcon(const DESKTOP_ICON *Icon, UINT32 X, UINT32 Y);
void TaskbarGeom(UINT32 *BarY, UINT32 *Sw, UINT32 *Sh);
void StartBtnGeom(UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH);
void MenuGeom(UINT32 *Mx, UINT32 *My, UINT32 *Mw, UINT32 *Mh);
int PointOccupied(UINT32 X, UINT32 Y);
void RequestRefresh(void);
void ClearIconFootprint(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void ClampIconPos(UINT32 *X, UINT32 *Y);
void ClampAllIcons(void);
void RedrawIconIndex(int Idx);
void SelectIcon(int Hit, UINT32 X, UINT32 Y, UINT64 Now);

void MenuCopyStr(char *Dst, int Max, const char *Src);
void RebuildStartMenu(void);
void AppsFlyoutGeom(UINT32 *Fx, UINT32 *Fy, UINT32 *Fw, UINT32 *Fh);

void FillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);
void DrawStringFree(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void DrawOneIconRaw(const DESKTOP_ICON *Icon, int Selected);
void DrawOneIconOccluded(const DESKTOP_ICON *Icon, int Selected);
void DrawTaskbarRaw(void);
void DrawStartMenuRaw(void);
void DrawTaskbarOccluded(void);

/* PR-N-nic-tray */
void DesktopNetTrayDraw(UINT32 ClockX, UINT32 TextY);
void DesktopNetTrayDrawPopup(void);
int DesktopNetTrayHandleClick(UINT32 X, UINT32 Y);
void DesktopNetTrayClose(void);
int DesktopNetTrayIsOpen(void);
int DesktopNetTrayLabelChanged(void);

int PathHasVolPrefix(const char *Path);
int LoadBmpPath(const char *Path, BMP_IMAGE *Out, UINT32 FileMax, const char *Tag);
void LoadDesktopIcons(void);
UINT32 BmpSampleScaled(const BMP_IMAGE *Img, UINT32 Dx, UINT32 Dy,
                       UINT32 Dw, UINT32 Dh);
void BlitBmpScaledRaw(UINT32 X, UINT32 Y, UINT32 Dw, UINT32 Dh,
                      const BMP_IMAGE *Img);
void BlitIconFaceRaw(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon);
void BlitIconFaceFree(UINT32 X, UINT32 Y, const DESKTOP_ICON *Icon);
void LoadIconLayout(void);
void SaveIconLayout(void);
void PlaceDesktopIcons(void);
void MoveIconTo(int Idx, UINT32 NewX, UINT32 NewY);

void FreeWallScreen(void);
void BuildWallScreen(void);
void LoadWallpaper(void);

#endif
