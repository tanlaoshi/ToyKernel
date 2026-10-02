/*
 * Desktop.c — 桌面图标 + 任务栏/开始菜单 + BMP 壁纸/图标（PR-S3-desktop-1）
 *
 * 时钟节流见 DesktopClock.c。开窗：桌面双击图标，或任务栏「开始」菜单。
 * PR-G-desk-1：图标可拖放；松手写入 TOYOS.DB；启动时 LoadIconLayout。
 * PR-G-desk-2：开始菜单动态列出 Apps/ 下 .ELF + 缺文件 INST(app) 灰显。
 * 壁纸/图标：优先 TOYOS:Assets/…（BI_RGB BMP）；读不到则纯色块。
 */
#include "DesktopPrivate.h"
#include "Gui.h" /* GuiCursorHide/Show：时钟重绘任务栏勿穿光标 */
#include "UiAction.h" /* PR-GUI-btn-action：DEBUG 桩 */

/* 全局定义集中在宿主；其它 TU 经 DesktopPrivate.h extern */
MENU_ROW gMenuRows[MENU_ROWS_MAX];
int gMenuCount;
MENU_ROW gMenuAppRows[MENU_APP_MAX];
int gMenuAppCount;
MENU_ROW gMenuGameRows[MENU_GAME_MAX];
int gMenuGameCount;
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
UINT32 gMenuCoverX;
UINT32 gMenuCoverY;
UINT32 gMenuCoverW;
UINT32 gMenuCoverH;
int gMenuAppsOpen;
int gMenuGameOpen;
UINT8 gClockHour;
UINT8 gClockMinute;
int gClockValid;

/* 已按当前分辨率拉伸的壁纸缓存（加速 DesktopFillRect，避免拖死鼠标） */
UINT32 *gWallScreen;
UINT32  gWallScreenW;
UINT32  gWallScreenH;
UINT32  gWallScreenPages;
int     gDesktopBusy; /* 防 DesktopInitialize / OnDisplayResize 重入 */

/* PR-R2：由 Gui 注册，Desktop 不 include Gui.h */
int (*gPointOccupied)(UINT32 X, UINT32 Y);
void (*gRequestRefresh)(void);
void (*gClearIconFootprint)(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);

int PointOccupied(UINT32 X, UINT32 Y) {
    return gPointOccupied ? gPointOccupied(X, Y) : 0;
}

void RequestRefresh(void) {
    if (gRequestRefresh) {
        gRequestRefresh();
    }
}

void ClearIconFootprint(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (gClearIconFootprint) {
        gClearIconFootprint(X, Y, W, H);
        return;
    }
    DesktopFillRectFree(X, Y, W, H);
    DesktopDrawRect(X, Y, W, H);
}

void DesktopSetPointOccupied(int (*Fn)(UINT32 X, UINT32 Y)) {
    gPointOccupied = Fn;
}

void DesktopSetRequestRefresh(void (*Fn)(void)) {
    gRequestRefresh = Fn;
}

void DesktopSetClearIconFootprint(void (*Fn)(UINT32 X, UINT32 Y, UINT32 W,
                                             UINT32 H)) {
    gClearIconFootprint = Fn;
}

/* 只画不被窗口盖住的像素 */

void DesktopDraw(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (!gIcons[i].Present) {
            continue;
        }
        DrawOneIconRaw(&gIcons[i], i == gDeskSelected);
    }
    DrawTaskbarRaw();
    /* 开始菜单不在此画：须叠在窗之上，见 DesktopDrawStartMenu */
}

void DesktopDrawStartMenu(void) {
    if (gMenuOpen) {
        DrawStartMenuRaw();
    }
}

void DesktopDrawNetTrayPopup(void) {
    DesktopNetTrayDrawPopup();
}

int DesktopStartMenuIsOpen(void) {
    return gMenuOpen ? 1 : 0;
}

void DesktopDismissStartMenu(void) {
    if (!gMenuOpen && !gMenuAppsOpen && !gMenuGameOpen) {
        return;
    }
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    DesktopNetTrayClose();
    RequestRefresh();
}

int DesktopClickOnTaskbar(UINT32 X, UINT32 Y) {
    UINT32 BarY;
    UINT32 Sw;
    UINT32 Sh;

    (void)X;
    TaskbarGeom(&BarY, &Sw, &Sh);
    return (Y >= BarY && Y < Sh) ? 1 : 0;
}

void DesktopNotifyAppsChanged(void) {
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    /* 装卸后重扫；Ensure 内 Rebuild + RequestRefresh */
    DesktopIconsResetDeferred();
    DesktopEnsureIconsLoaded();
}

void DesktopInitialize(void) {
    if (gDesktopBusy) {
        DebugWrite("desktop: Init reenter ignored\n");
        return;
    }
    gDesktopBusy = 1;

    /* PR-BOOT-fast-1：只占位+布局；BMP/动态图标/菜单扫盘见 DesktopEnsureIconsLoaded */
    DesktopIconsResetDeferred();
    PlaceDesktopIcons();
    LoadIconLayout();

    gDeskSelected = -1;
    gSelectClock = 0;
    gSelectX = 0;
    gSelectY = 0;
    gMenuOpen = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    gMenuCount = 0;
    gMenuAppCount = 0;
    gMenuGameCount = 0;
    DesktopNetTrayClose();
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    LoadWallpaper();
    ToyLogGui("Boot: Desktop Ready\n");
    /* PR-G-igpu-corner：不再画右上角品红自测块（原 PR-G-igpu-3） */
    /* PR-G-audio-4：桌面 Ready 后再短鸣，验收 HalAudio API */
    HalAudioBeep();
    DebugWrite("desktop: solid ready (icons deferred)\n");
#if TOY_KERNEL_DEBUG
    UiActionSelfCheck();
#endif
    gDesktopBusy = 0;
    /*
     * PR-BOOT-fast-1 本意 defer 给 Worker；但 UP（TOY_SMP=1）上 worker 与
     * shell/gui 同核且 prio 更低，Halt 后仍优先交互任务 → Icons Loaded 永不出现。
     * 在 Init 末同步加载：首帧 Compose 前就有 BMP；Worker 再 Ensure 即 no-op。
     */
    DesktopEnsureIconsLoaded();
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
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    FreeWallScreen();
    if (ThemeWallpaperEnabled() && gWallReady) {
        BuildWallScreen();
    }
    gDesktopBusy = 0;
}

void DesktopRefreshLabels(void) {
    gIcons[0].Label = LocStr(MSG_ICON_SHELL);
    gIcons[1].Label = LocStr(MSG_ICON_SETTINGS);
    gIcons[2].Label = LocStr(MSG_ICON_FILES);
    gIcons[3].Label = LocStr(MSG_ICON_STORE);
    gIcons[4].Label = LocStr(MSG_ICON_DEVICES);
}
