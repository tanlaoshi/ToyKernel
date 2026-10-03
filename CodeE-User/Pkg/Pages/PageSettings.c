/*
 * PageSettings.c — 设置页（策略 A 1100–1199；导航 9000 独立）
 */
#include <ToyUi.h>
#include "Ids.h"
#include "PageSettings.h"

void SettingsCreate(int Wid) {
    ToyUiSetLabel(Wid, "Settings 1100  nav=9001");
    ToyUiAddButton(Wid, 0, "OK");
    ToyUiAddButton(Wid, 1, "->Home");
}

void SettingsDestroy(int Wid) {
    ToyUiSetLabel(Wid, "");
}

void SettingsDispatch(int Wid, int Id) {
    if (Id == ID_SET_OK) {
        ToyUiSetLabel(Wid, "Settings OK 1100");
    }
}
