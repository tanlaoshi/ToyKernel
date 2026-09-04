/*
 * Gui.h — 简易窗口管理器与鼠标光标
 */
#ifndef GUI_H
#define GUI_H

#include "BootTypes.h"

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
} GUI_MOUSE_STATE;

/* PR-D3：窗口种类（桌面图标 / Settings 依赖） */
typedef enum {
    GUI_WIN_NONE = 0,
    GUI_WIN_SHELL,
    GUI_WIN_SETTINGS,
    GUI_WIN_FILES
} GUI_WIN_KIND;

#define GUI_MAX_WINS        6
#define GUI_TITLE_HEIGHT    40
#define GUI_CLIENT_PAD 8
#define GUI_INPUT_LINE_MAX  128

void GuiInit(void);
void GuiPointerMove(UINT32 X, UINT32 Y);
void GuiCursorPaint(void);
void GuiFrameBufferBegin(void);
void GuiFrameBufferEnd(void);
void GuiCursorHide(void);
void GuiCursorShow(void);
void GuiOnMouse(const GUI_MOUSE_STATE *Mouse);
void GuiOnArrowKey(UINT8 Key);
void GuiRedraw(void);
/* PR-D2：按 Theme 刷新已开窗客户区底色（不立即重绘） */
void GuiApplyThemeColors(void);
/* PR-G8：桌面+各窗内容自下而上一次合成并备份 */
void GuiComposeThemeScene(void);
/* 按当前几何重画整窗（标题栏+客户区底）；不画 Shell 文字 / Settings 菜单 */
void GuiPaintWindow(int Idx);
void GuiBackupAllWindows(void);
/* 点是否落在任一应用窗内（桌面图标绘制避让） */
int GuiPointInAnyWindow(UINT32 X, UINT32 Y);
int GuiHandleClick(UINT32 X, UINT32 Y);
int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background);
void GuiFocusSave(void);
void GuiFocusApply(void);
void GuiFocusApplyClip(void);
void GuiFocusSyncCursor(void);
void GuiBackupSyncRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* 把焦点窗当前帧缓冲整窗抓进备份（Shell/Settings 画完内容后调用） */
void GuiBackupFocusWindow(void);
void GuiFocusClearClient(void);
int GuiShellAcceptsInput(void);
void GuiFocusHome(void);
void GuiPollMouse(void);
int GuiShellWindowActive(int Idx);
void GuiSetFocusWin(int Idx);
/* 置顶 + 从备份重合成；内容绘制前应调用，避免写穿上层窗 */
void GuiRaiseToFront(int Idx);
/* 当前焦点窗下标；无焦点返回 -1 */
int GuiFocusIndex(void);

/* PR-D3：打开应用窗；成功返回槽位，失败 -1 */
int GuiOpenShell(void);
int GuiOpenSettings(void);
int GuiOpenFiles(void);
/* PR-I18N2：按当前语言刷新窗标题并重绘 chrome */
void GuiRefreshTitles(void);
GUI_WIN_KIND GuiFocusKind(void);
GUI_WIN_KIND GuiWindowKind(int Idx);

/* PR-G2：每窗 Shell 输入行与提示符状态（随 GUI_WINDOW 移动） */
void GuiConsolePull(char *Line, int *Len, int *WaitPrompt);
void GuiConsolePush(const char *Line, int Len, int WaitPrompt);
int GuiConsoleNeedsPrompt(void);
void GuiConsoleMarkPrompt(void);
int GuiConsoleHasDisplay(void);
/* 焦点不在 Shell 时：标记所有 Shell 下次获焦需补 prompt */
void GuiShellRequestPrompt(void);

#endif
