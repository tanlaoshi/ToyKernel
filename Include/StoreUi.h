/*
 * StoreUi.h — 商店 GUI（PR-G-store-ui）
 */
#ifndef STORE_UI_H
#define STORE_UI_H

#include "BootTypes.h"

void StoreUiOpen(void);
void StoreUiPaintFocused(void);
void StoreUiRepaint(void);
void StoreUiOnClick(UINT32 X, UINT32 Y);
int StoreUiIsFocused(void);

#endif
