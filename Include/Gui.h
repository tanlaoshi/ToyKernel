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
int GuiHandleClick(UINT32 X, UINT32 Y);
int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background);
void GuiFocusSave(void);
void GuiFocusApply(void);
void GuiFocusApplyClip(void);
void GuiFocusSyncCursor(void);
void GuiFocusClearClient(void);
int GuiShellAcceptsInput(void);
void GuiFocusHome(void);
void GuiPollMouse(void);

/* PR-G2：每窗 Shell 输入行与提示符状态（随 GUI_WINDOW 移动） */
void GuiConsolePull(char *Line, int *Len, int *WaitPrompt);
void GuiConsolePush(const char *Line, int Len, int WaitPrompt);
int GuiConsoleNeedsPrompt(void);
void GuiConsoleMarkPrompt(void);
int GuiConsoleHasDisplay(void);

#endif
