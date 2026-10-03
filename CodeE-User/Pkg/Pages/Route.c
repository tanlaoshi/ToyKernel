/*
 * Route.c — 页面表（§2.2）+ 导航 9000（§4）+ Destroy/Create
 * PR-UI-page-1：物理槽 0..3 → 逻辑 ID；导航优先于页面 Dispatch。
 * PR-UI-page-2：加 PageAbout（3 页互通导航）。
 */
#include <ToyUi.h>
#include "Ids.h"
#include "PageHome.h"
#include "PageSettings.h"
#include "PageAbout.h"
#include "Route.h"

typedef struct {
    void (*Create)(int Wid);
    void (*Destroy)(int Wid);
    void (*Dispatch)(int Wid, int Id);
} APP_PAGE;

static const APP_PAGE gPages[PAGE_N] = {
    { HomeCreate,     HomeDestroy,     HomeDispatch     },
    { SettingsCreate, SettingsDestroy, SettingsDispatch },
    { AboutCreate,    AboutDestroy,    AboutDispatch    },
};

static int s_Page = PAGE_HOME;

static int MapEv(int Ev) {
    if (s_Page == PAGE_HOME) {
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            return ID_HOME_PING;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(1)) {
            return ID_NAV_SETTINGS;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(2)) {
            return ID_NAV_ABOUT;
        }
    } else if (s_Page == PAGE_SETTINGS) {
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            return ID_SET_OK;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(1)) {
            return ID_NAV_HOME;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(2)) {
            return ID_NAV_ABOUT;
        }
    } else { /* PAGE_ABOUT：§2.3 表驱动页内分发，这里只做槽→逻辑 ID 映射 */
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            return ID_ABOUT_INFO;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(1)) {
            return ID_ABOUT_HELP;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(2)) {
            return ID_NAV_HOME;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(3)) {
            return ID_NAV_SETTINGS;
        }
    }
    return Ev;
}

void RouteInit(int Wid) {
    s_Page = PAGE_HOME;
    gPages[PAGE_HOME].Create(Wid);
}

void RouteSetPage(int Wid, int Page) {
    if (Page < 0 || Page >= PAGE_N || Page == s_Page) {
        return;
    }
    gPages[s_Page].Destroy(Wid);
    s_Page = Page;
    gPages[Page].Create(Wid);
}

int RoutePage(void) {
    return s_Page;
}

int RouteDispatch(int Wid, int Ev) {
    int Id;

    if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
        return 1;
    }
    Id = MapEv(Ev);
    /* 导航优先：不属于任何页面（§4） */
    if (Id == ID_NAV_HOME) {
        ToyUiSetLabel(Wid, "nav 9000 ->Home");
        RouteSetPage(Wid, PAGE_HOME);
        return 0;
    }
    if (Id == ID_NAV_SETTINGS) {
        ToyUiSetLabel(Wid, "nav 9001 ->Settings");
        RouteSetPage(Wid, PAGE_SETTINGS);
        return 0;
    }
    if (Id == ID_NAV_ABOUT) {
        ToyUiSetLabel(Wid, "nav 9002 ->About");
        RouteSetPage(Wid, PAGE_ABOUT);
        return 0;
    }
    gPages[s_Page].Dispatch(Wid, Id);
    return 0;
}
