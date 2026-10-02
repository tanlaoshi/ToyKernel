/*
 * SettingsUiInput.c — 键鼠与 Esc
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

void SettingsUiOnEscape(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    /* 三分栏无「返回上级」；Esc 仅刷新 */
    PaintMenu();
}

void SettingsUiOnDigit(char Digit) {
    int N;

    if (!SettingsUiIsFocused()) {
        return;
    }
    if (Digit < '1' || Digit > '9') {
        return;
    }
    N = Digit - '1';
    if (N < ItemCount()) {
        ApplyItem(N);
    }
}

void SettingsUiOnClick(UINT32 X, UINT32 Y) {
    int i;
    int First;

    if (!SettingsUiIsFocused()) {
        return;
    }
    if (gSetSbVisible &&
        UiScrollBarHit(gSetSbX, gSetSbY, gSetSbW, gSetSbH, gItemScroll, gSetListVisible,
                       ItemCount(), X, Y, &First)) {
        gItemScroll = First;
        SettingsUiRepaint();
        return;
    }
    for (i = 0; i < gSetHitCount; i++) {
        if (!UiHitRect(gSetHits[i].X, gSetHits[i].Y, gSetHits[i].W, gSetHits[i].H, X, Y)) {
            continue;
        }
        if (gSetHits[i].Kind == 0) {
            SelectCategory((SETTINGS_CAT)gSetHits[i].Index);
            SettingsUiRepaint();
            return;
        }
        if (gSetHits[i].Kind == 1) {
            ApplyItem(gSetHits[i].Index);
            return;
        }
    }
}

void SettingsUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons) {
    int i;
    int Kind = -1;
    int Idx = -1;
    int Need = 0;
    int FireKind = -1;
    int FireIdx = -1;
    static UINT8 sPrevBtn;

    if (!SettingsUiIsFocused()) {
        if (gSetHoverKind >= 0 || gSetPressKind >= 0) {
            gSetHoverKind = -1;
            gSetHoverIdx = -1;
            gSetPressKind = -1;
            gSetPressIdx = -1;
        }
        sPrevBtn = Buttons;
        return;
    }
    for (i = 0; i < gSetHitCount; i++) {
        if (UiHitRect(gSetHits[i].X, gSetHits[i].Y, gSetHits[i].W, gSetHits[i].H, X, Y)) {
            Kind = gSetHits[i].Kind;
            Idx = gSetHits[i].Index;
            break;
        }
    }
    if (Kind != gSetHoverKind || Idx != gSetHoverIdx) {
        gSetHoverKind = Kind;
        gSetHoverIdx = Idx;
        Need = 1;
    }
    if ((Buttons & 1u) && !(sPrevBtn & 1u)) {
        if (Kind >= 0) {
            gSetPressKind = Kind;
            gSetPressIdx = Idx;
            Need = 1;
        }
    } else if ((Buttons & 1u) && gSetPressKind >= 0 &&
               (Kind != gSetPressKind || Idx != gSetPressIdx)) {
        gSetPressKind = -1;
        gSetPressIdx = -1;
        Need = 1;
    } else if (!(Buttons & 1u) && (sPrevBtn & 1u) && gSetPressKind >= 0) {
        if (Kind == gSetPressKind && Idx == gSetPressIdx) {
            FireKind = gSetPressKind;
            FireIdx = gSetPressIdx;
        }
        gSetPressKind = -1;
        gSetPressIdx = -1;
        Need = 1;
    }
    sPrevBtn = Buttons;
    if (Need) {
        SettingsUiRepaint();
    }
    if (FireKind == 0) {
        SelectCategory((SETTINGS_CAT)FireIdx);
        SettingsUiRepaint();
    } else if (FireKind == 1) {
        ApplyItem(FireIdx);
    }
}
