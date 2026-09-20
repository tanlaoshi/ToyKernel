/*
 * ToyUiPrivate.h — PR-S-toyui-1：libToyUi 模块内共享
 */
#ifndef TOY_UI_PRIVATE_H
#define TOY_UI_PRIVATE_H

#include <ToyUi.h>

#define TOY_UI_WIN_MAX 6
#define TOY_UI_CLICK_PACK_BASE 400
#define TOY_UI_CLICK_SHIFT 10
#define TOY_UI_CLICK_MASK 1023
#define TOY_UI_CHECK_SIZE 16u
#define TOY_UI_LIST_ROW 18u
#define TOY_UI_FIELD_H 22u

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    int Checked;
} TOY_UI_CHECK;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Count;
    int Selected;
    char Items[TOY_UI_LIST_ITEM_MAX][16];
} TOY_UI_LIST;

typedef struct {
    int Used;
    unsigned X;
    unsigned Y;
    unsigned W;
    int Focus;
    char Text[TOY_UI_TEXT_MAX];
} TOY_UI_FIELD;

typedef struct {
    TOY_UI_CHECK Check[TOY_UI_CHECK_ID_MAX + 1];
    TOY_UI_LIST List;
    TOY_UI_FIELD Field;
} TOY_UI_WIN;

TOY_UI_WIN *ToyUiWinState(int WindowId);
int ToyUiInRect(unsigned X, unsigned Y, unsigned Rx, unsigned Ry, unsigned Rw,
                unsigned Rh);
void ToyUiCopyCap(char *Dst, unsigned Cap, const char *Src);
void ToyUiRedrawWin(int WindowId, TOY_UI_WIN *St);
int ToyUiHitWidgets(int WindowId, unsigned X, unsigned Y);

#endif
