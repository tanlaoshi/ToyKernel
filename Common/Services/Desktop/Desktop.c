/*
 * Desktop.c — 桌面图标 + 任务栏/开始菜单 + BMP 壁纸/图标（PR-D4 / PR-G13）
 *
 * 开窗：桌面双击图标，或任务栏「开始」菜单（不单靠图标）。
 * PR-G-desk-1：图标可拖放；松手写入 TOYOS.DB（ic0..ic3=x,y）；启动时 LoadIconLayout。
 * PR-G-desk-2：开始菜单动态列出 Apps/ 下 .ELF + 缺文件 INST(app) 灰显；点选 ProcessExec。
 * 壁纸/图标：优先 TOYOS:Assets/…（BI_RGB BMP）；读不到则纯色块（不内嵌像素、不走 RES:）。
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
int     gDesktopBusy; /* 防 DesktopInit / OnDisplayResize 重入 */

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
    gMenuCount = 0;
    gMenuAppCount = 0;
    gMenuGameCount = 0;
    gMenuAppsOpen = 0;
    gMenuGameOpen = 0;
    DesktopLoadAppIcons();
    LoadIconLayout();
    LoadDesktopIcons();
    if (gMenuOpen) {
        RebuildStartMenu();
    }
    RequestRefresh();
}

void DesktopInit(void) {
    if (gDesktopBusy) {
        DebugWrite("desktop: Init reenter ignored\n");
        return;
    }
    gDesktopBusy = 1;

    PlaceDesktopIcons();
    DesktopLoadAppIcons();
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
    LoadDesktopIcons();
    ToyLogGui("Boot: Desktop Ready\n");
    DebugWrite("desktop: icons+taskbar ready (TOYOS Assets or solid)\n");
#if TOY_KERNEL_DEBUG
    /* PR-GUI-btn-action：桌面就绪后串口自检 SYNC/ASYNC 分发命中 */
    UiActionSelfCheck();
#endif
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
    gIcons[5].Label = LocStr(MSG_ICON_SNAKE);
}

void DesktopTickClock(void) {
    static UINT64 LastCheckTick;
    UINT64 Now;
    UINT32 Tps;
    UINT8 Hour = 0;
    UINT8 Minute = 0;
    int Ok;
    int NeedPaint;

    /*
     * 墙钟节流（勿用 Poll 计数）：Gui+Shell 双路径轮询时 Skip=45 几乎每帧
     * 撞 CMOS。UIP 约 1Hz；旧逻辑读失败还 NeedPaint→整条任务栏 Present，
     * 鼠标滑动时体感「约 1 秒顿一次」（全系统顿挫，非仅光标采样）。
     */
    /* 拖窗/改大小时不读 CMOS、不重画任务栏，避免这一拍把鼠标卡住 */
    if (GuiDragActive() || DesktopIconDragActive()) {
        return;
    }

    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    if (LastCheckTick != 0 && (Now - LastCheckTick) < (UINT64)(Tps / 2u)) {
        return;
    }
    LastCheckTick = Now;

    /* PR-H-msc-hot：~2Hz 拔出探测（仅 CCS/hub；不 Address） */
    if (HalUsbMscHotPoll() == 1) {
        (void)FileSystemRemountVolumes();
        DebugWrite("desktop: msc hot remount after unplug\n");
    }

    NeedPaint = 0;
    Ok = (HalRtcGetTime(0, 0, 0, &Hour, &Minute, 0) == 0) ? 1 : 0;
    if (Ok) {
        if (!(gClockValid && Hour == gClockHour && Minute == gClockMinute)) {
            NeedPaint = 1;
        }
    }
    /* 瞬时读失败：保留上次 HH:MM，勿刷 --:-- / 勿整栏 Present */
    if (DesktopNetTrayLabelChanged()) {
        NeedPaint = 1;
    }
    if (!NeedPaint) {
        return;
    }
    /*
     * 勿 BeginFront：scale≠100 时逻辑坐标直写物理 GOP → 假任务栏。
     * 后缓冲 + Present；先擦光标再铺栏，避 Alpha/开始钮烙印。
     * 须 ClearClip：Shell 客户区 clip 会让半透底被裁、开始图标仍在。
     */
    {
        UINT32 BarY;
        UINT32 Sw;
        UINT32 Sh;

        TaskbarGeom(&BarY, &Sw, &Sh);
        HalVideoClearClip();
        GuiCursorHide();
        DesktopFillRect(0, BarY, Sw, TASKBAR_H);
        DrawTaskbarRaw();
        if (gMenuOpen) {
            DrawStartMenuRaw();
        }
        if (DesktopNetTrayIsOpen()) {
            DesktopNetTrayDrawPopup();
        }
        GuiCursorShow();
        HalVideoPresent();
    }
}
