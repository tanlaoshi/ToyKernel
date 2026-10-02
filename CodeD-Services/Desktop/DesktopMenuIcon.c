/*
 * DesktopMenuIcon.c — 开始菜单行图标（关机/重启专用 BMP）
 */
#include "DesktopPrivate.h"

/* 返回 1 已画图标（调用方右移文字） */
int DrawMenuRowIcon(const MENU_ROW *R, UINT32 IconX, UINT32 IconY) {
    if (!R) {
        return 0;
    }
    /* Action 优先：IconSrc 6/7 曾与 Apps 槽冲突 */
    if (R->Action == DESKTOP_ACTION_SHUTDOWN) {
        if (gPowerBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, &gPowerBmp);
        } else {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            ThemeIconFallbackPower());
        }
        return 1;
    }
    if (R->Action == DESKTOP_ACTION_REBOOT) {
        if (gRebootBmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ, &gRebootBmp);
        } else {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            ThemeIconFallbackReboot());
        }
        return 1;
    }
    if (R->IconSrc >= 0 && R->IconSrc < DESKTOP_ICON_COUNT) {
        if (gIcons[R->IconSrc].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[R->IconSrc].Bmp);
        } else {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[R->IconSrc].IconColor);
        }
        return 1;
    }
    if (R->Action == DESKTOP_ACTION_EXEC) {
        /* IconSrc<0：无桌面槽时勿回落 Shell，用 Store 作通用应用标 */
        if (gIcons[3].BmpReady) {
            BlitBmpScaledRaw(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                             &gIcons[3].Bmp);
        } else {
            UiFillRectangle(IconX, IconY, MENU_ICON_SZ, MENU_ICON_SZ,
                            gIcons[3].IconColor);
        }
        return 1;
    }
    return 0;
}
