/*
 * Gui.h — 简易窗口管理器与鼠标光标
 */
#ifndef GUI_H
#define GUI_H

#include "BootConfig.h"

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
} GUI_MOUSE_STATE;

#define GUI_TITLE_H    24
#define GUI_CLIENT_PAD 6

void GuiInit(void);
void GuiPointerMove(UINT32 X, UINT32 Y);
void GuiCursorPaint(void);
void GuiFbBegin(void);
void GuiFbEnd(void);
void GuiCursorHide(void);
void GuiCursorShow(void);
void GuiOnMouse(const GUI_MOUSE_STATE *Mouse);
void GuiOnArrowKey(UINT8 Key);
void GuiRedraw(void);
int GuiHandleClick(UINT32 X, UINT32 Y);
int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H, UINT32 *Bg);
void GuiFocusSave(void);
void GuiFocusApply(void);
void GuiFocusHome(void);
void GuiPollMouse(void);

#endif
