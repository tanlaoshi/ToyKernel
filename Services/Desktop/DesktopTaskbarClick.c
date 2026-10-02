/*
 * DesktopTaskbarClick.c — 任务栏 / 开始菜单点击（PR-S3-desktopclick-1）
 *
 * 从 DesktopClick.c 原样搬家；不改语义。
 */
#include "DesktopPrivate.h"

int HandleTaskbarClick(UINT32 X, UINT32 Y, DESKTOP_ACTION *OutAction,
                       char *OutExecPath, UINTN ExecPathMax) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;
    UINT32 Fx;
    UINT32 Fy;
    UINT32 Fw;
    UINT32 Fh;
    UINT32 Gx;
    UINT32 Gy;
    UINT32 Gw;
    UINT32 Gh;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    int Item;
    int InMain;
    int InFly;
    int InGame;
    int OnStart;

    if (OutExecPath && ExecPathMax > 0) {
        OutExecPath[0] = 0;
    }

    TaskbarGeom(&BarY, &Sw, &Sh);
    StartBtnGeom(&Bx, &By, &Bw, &Bh);
    OnStart = (Y >= By && Y < By + Bh && X >= Bx && X < Bx + Bw) ? 1 : 0;

    /* 网络托盘弹层 / 短文案优先 */
    if (DesktopNetTrayHandleClick(X, Y)) {
        if (OutAction) {
            *OutAction = DESKTOP_ACTION_NONE;
        }
        return 1;
    }

    if (gMenuOpen) {
        /*
         * 再点开始钮：只收起，勿先关再 toggle 打开。
         */
        if (OnStart) {
            gMenuOpen = 0;
            gMenuAppsOpen = 0;
            gMenuGameOpen = 0;
            RequestRefresh();
            return 1;
        }

        MenuGeom(&Mx, &My, &Mw, &Mh);
        InMain = (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) ? 1 : 0;
        InFly = 0;
        InGame = 0;
        if (gMenuAppsOpen) {
            AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
            InFly = (X >= Fx && Y >= Fy && X < Fx + Fw && Y < Fy + Fh) ? 1 : 0;
        }
        if (gMenuGameOpen) {
            GameFlyoutGeom(&Gx, &Gy, &Gw, &Gh);
            InGame = (X >= Gx && Y >= Gy && X < Gx + Gw && Y < Gy + Gh) ? 1 : 0;
        }

        if (InGame) {
            Item = (int)((Y - Gy) / MENU_ITEM_H);
            if (Item >= 0 && Item < gMenuGameCount) {
                MENU_ROW *R = &gMenuGameRows[Item];
                DESKTOP_ACTION Act = R->Action;

                gMenuOpen = 0;
                gMenuAppsOpen = 0;
                gMenuGameOpen = 0;
                RequestRefresh();
                if (!R->Enabled) {
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (OutAction) {
                    *OutAction = Act;
                }
                if (Act == DESKTOP_ACTION_EXEC && OutExecPath &&
                    ExecPathMax > 0) {
                    MenuCopyStr(OutExecPath, (int)ExecPathMax, R->Path);
                }
                return 1;
            }
            return 1;
        }

        if (InFly) {
            Item = (int)((Y - Fy) / MENU_ITEM_H);
            if (Item >= 0 && Item < gMenuAppCount) {
                MENU_ROW *R = &gMenuAppRows[Item];
                DESKTOP_ACTION Act = R->Action;

                gMenuOpen = 0;
                gMenuAppsOpen = 0;
                gMenuGameOpen = 0;
                if (Act != DESKTOP_ACTION_SHUTDOWN &&
                    Act != DESKTOP_ACTION_REBOOT) {
                    RequestRefresh();
                }
                if (!R->Enabled) {
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (OutAction) {
                    *OutAction = Act;
                }
                if (Act == DESKTOP_ACTION_EXEC && OutExecPath &&
                    ExecPathMax > 0) {
                    MenuCopyStr(OutExecPath, (int)ExecPathMax, R->Path);
                }
                return 1;
            }
            /* flyout 空白区：吞掉 */
            return 1;
        }

        if (InMain) {
            Item = (int)((Y - My) / MENU_ITEM_H);
            if (Item >= 0 && Item < gMenuCount) {
                MENU_ROW *R = &gMenuRows[Item];
                DESKTOP_ACTION Act = R->Action;

                if (Act == DESKTOP_ACTION_NONE ||
                    R->IconSrc == MENU_ICON_SRC_SEP) {
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (Act == DESKTOP_ACTION_APPS) {
                    gMenuAppsOpen = !gMenuAppsOpen;
                    gMenuGameOpen = 0;
                    RequestRefresh();
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (Act == DESKTOP_ACTION_GAME) {
                    gMenuGameOpen = !gMenuGameOpen;
                    gMenuAppsOpen = 0;
                    RequestRefresh();
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }

                gMenuOpen = 0;
                gMenuAppsOpen = 0;
                gMenuGameOpen = 0;
                if (Act != DESKTOP_ACTION_SHUTDOWN &&
                    Act != DESKTOP_ACTION_REBOOT) {
                    RequestRefresh();
                }
                if (!R->Enabled) {
                    if (OutAction) {
                        *OutAction = DESKTOP_ACTION_NONE;
                    }
                    return 1;
                }
                if (OutAction) {
                    *OutAction = Act;
                }
                if (Act == DESKTOP_ACTION_EXEC && OutExecPath &&
                    ExecPathMax > 0) {
                    MenuCopyStr(OutExecPath, (int)ExecPathMax, R->Path);
                }
                return 1;
            }
        }

        /* 点在菜单外：关菜单；点到窗则由 Gui 继续 Raise */
        gMenuOpen = 0;
        gMenuAppsOpen = 0;
        gMenuGameOpen = 0;
        RequestRefresh();
        if (Y >= BarY && Y < Sh) {
            return 1; /* 任务栏其它区域吞掉 */
        }
        return 0;
    }

    if (Y >= BarY && Y < Sh) {
        if (OnStart) {
            gMenuOpen = 1;
            gMenuAppsOpen = 0;
            gMenuGameOpen = 0;
            DesktopNetTrayClose();
            /* Apps 未变则复用上次 Rebuild；NotifyAppsChanged 会清 gMenuCount */
            if (gMenuCount <= 0) {
                RebuildStartMenu();
            }
            RequestRefresh();
            return 1;
        }
        return 1; /* 任务栏其它区域：吞掉 */
    }
    return 0;
}
