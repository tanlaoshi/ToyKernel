/*
 * TtyUiPrivate.h — TtyUi 内部（仅 Services/TtyUi 下 .c）
 */
#ifndef TTY_UI_PRIVATE_H
#define TTY_UI_PRIVATE_H

#include "TtyUi.h"
#include "Gui.h"
#include "GuiPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalSerial.h"
#include "Font.h"
#include "Theme.h"
#include "Debug.h"

#define TTY_BUF_MAX    4096
#define TTY_STATUS_MAX 96

extern char gTtyBuf[TTY_BUF_MAX];
extern UINTN gTtyLen;
extern int gTtyScroll;
extern char gTtyStatus[TTY_STATUS_MAX];
extern int gTtyOpen;

void TtyAppend(char C);
void TtyAppendStr(const char *S);
void TtySetStatus(const char *S);
void TtySerialOut(const char *S);
void TtyClampScroll(UINT32 VisLines);
void TtyPaint(void);

#endif
