/*
 * Desktop.h — 桌面图标、任务栏与壁纸（PR-D4 / PR-G13）
 *
 * PR-R2：Desktop 不 #include Gui/Console；只发 DESKTOP_ACTION，由 Gui 开窗。
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "BootTypes.h"

typedef enum {
    DESKTOP_ACTION_NONE = -1,
    DESKTOP_ACTION_SHELL = 0,
    DESKTOP_ACTION_SETTINGS,
    DESKTOP_ACTION_FILES,
    DESKTOP_ACTION_STORE,
    DESKTOP_ACTION_DEVICES,
    DESKTOP_ACTION_SHUTDOWN,
    DESKTOP_ACTION_REBOOT,
    DESKTOP_ACTION_EXEC, /* PR-G-desk-2：跑 Apps/ 下 .ELF；路径见 OutExecPath */
    DESKTOP_ACTION_APPS  /* 开始菜单 Apps 一级；二级 flyout 列已装 ELF */
} DESKTOP_ACTION;

/* Store 装卸后：若开始菜单开着则重建（含 Apps 二级） */
void DesktopNotifyAppsChanged(void);

void DesktopInit(void);
/* PR-G-hotres：分辨率热切后重建壁纸缓存/图标坐标，不重读 FAT（避免长循环重入） */
void DesktopOnDisplayResize(void);
/* Gui 注册：点是否被窗占用（图标避让）；菜单开合时请求刷新桌面 */
void DesktopSetPointOccupied(int (*Fn)(UINT32 X, UINT32 Y));
void DesktopSetRequestRefresh(void (*Fn)(void));
/* 擦图标脚印并还原相交窗/阴影（拖动置顶后用） */
void DesktopSetClearIconFootprint(void (*Fn)(UINT32 X, UINT32 Y, UINT32 W,
                                             UINT32 H));
/* 在桌面背景上画图标+任务栏（GuiRedraw / 关窗露底后调用） */
void DesktopDraw(void);
/* 开始菜单弹出层（开着时叠画在窗上；点菜单外则收起，再按窗聚焦） */
void DesktopDrawStartMenu(void);
/* PR-N-nic-tray：网络简况弹层（叠在窗上） */
void DesktopDrawNetTrayPopup(void);
int DesktopStartMenuIsOpen(void);
/* PR-N-nic-tray */
int DesktopNetTrayIsOpen(void);
void DesktopDismissStartMenu(void);
/* 点在任务栏条带上时优先于窗（开始钮）；菜单开时由 Gui 先走 DesktopHandleClick */
int DesktopClickOnTaskbar(UINT32 X, UINT32 Y);
/* 仅重绘与矩形相交的图标/任务栏（关窗擦除区域） */
void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* PR-G13：壁纸或 ThemeDesktopBackground 填矩形 */
void DesktopFillRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* 避让窗口：图标拖动擦脚印用，勿盖标题栏/客户区 */
void DesktopFillRectFree(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
UINT32 DesktopBgAt(UINT32 X, UINT32 Y);
/*
 * 采样桌面图标/任务栏像素。
 * 命中不透明像素返回 1 并写 *Out；否则返回 0（调用方用 DesktopBgAt）。
 */
int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out);
/*
 * 桌面空白处的按下：任务栏/开始菜单，或双击图标。
 * 返回 1=已处理；*OutAction 为待开应用（NONE 表示仅菜单/选中）。
 * PR-G-desk-1：单击图标会武装拖放（与窗标题拖并存，互斥）。
 * PR-G-desk-2：OutAction==EXEC 时写入 OutExecPath（如 Apps/HELLO.ELF）。
 */
int DesktopHandleClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction,
                       char *OutExecPath, UINTN ExecPathMax);
/* PR-G-desk-1：图标拖放（Gui 在按住左键时调用） */
int DesktopIconDragActive(void);
void DesktopIconDragUpdate(UINT32 X, UINT32 Y);
void DesktopIconDragEnd(void);
void DesktopRefreshLabels(void);
/* PR-G-taskbar-clock：分钟变化时重绘右下角时间 */
void DesktopTickClock(void);

#endif
