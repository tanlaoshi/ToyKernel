/*
 * DevicesUiPrivate.h — DevicesUi 内部（仅 Common/Services/DevicesUi）
 */
#ifndef DEVICES_UI_PRIVATE_H
#define DEVICES_UI_PRIVATE_H

#include "DevicesUi.h"
#include "Device.h"
#include "Driver.h"
#include "Gui.h"
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "UI.h"

#define DEVUI_ROW_H     22u
#define DEVUI_PAD       8u
#define DEVUI_DETAIL_H  120u

extern int gDevUiSel;
extern int gDevUiScroll;
extern int gDevUiCount;
extern UINT32 gDevUiListX;
extern UINT32 gDevUiListY;
extern UINT32 gDevUiListW;
extern UINT32 gDevUiListH;
extern int gDevUiVisible;

void DevicesUiReload(void);
void DevicesUiFormatPci(const DEVICE_NODE *Dev, char *Out, UINTN Max);
void DevicesUiFormatIds(const DEVICE_NODE *Dev, char *Out, UINTN Max);
void DevicesUiPaint(void);

#endif
