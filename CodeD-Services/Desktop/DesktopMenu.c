/*
 * DesktopMenu.c — 开始菜单重建（系统项 + Game；Apps 见 DesktopMenuApps.c）
 */
#include "DesktopPrivate.h"

void MenuCopyStr(char *Dst, int Max, const char *Src) {
    int i;

    if (!Dst || Max <= 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i < Max - 1; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

int MenuNameEqIgnoreCase(const char *A, const char *B) {
    char Ca;
    char Cb;

    if (!A || !B) {
        return 0;
    }
    while (*A && *B) {
        Ca = *A;
        Cb = *B;
        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static int MenuMaxAppSlots(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Room;
    int MaxRows;

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)Sw;
    Room = BarY > 8u ? (BarY - 8u) : 0;
    MaxRows = (int)(Room / MENU_ITEM_H);
    if (MaxRows < 1) {
        MaxRows = 1;
    }
    if (MaxRows > MENU_APP_MAX) {
        MaxRows = MENU_APP_MAX;
    }
    return MaxRows;
}

static void MenuAddRow(DESKTOP_ACTION Act, const char *Label, const char *Path,
                       int Enabled, int IconSrc) {
    MENU_ROW *R;

    if (gMenuCount >= MENU_ROWS_MAX) {
        return;
    }
    R = &gMenuRows[gMenuCount++];
    R->Action = Act;
    R->Enabled = Enabled ? 1 : 0;
    R->IconSrc = IconSrc;
    MenuCopyStr(R->Label, sizeof(R->Label), Label ? Label : "");
    MenuCopyStr(R->Path, sizeof(R->Path), Path ? Path : "");
}

/* 打开开始菜单时重建：系统项含 Apps/Game；Apps 二级见 FillStartMenuAppRows */
void RebuildStartMenu(void) {
    const char *L;

    gMenuCount = 0;
    gMenuAppCount = 0;
    gMenuGameCount = 0;

    L = LocStr(MSG_ICON_SHELL);
    MenuAddRow(DESKTOP_ACTION_SHELL, L ? L : "Shell", 0, 1, 0);
    L = LocStr(MSG_ICON_SETTINGS);
    MenuAddRow(DESKTOP_ACTION_SETTINGS, L ? L : "Settings", 0, 1, 1);
    L = LocStr(MSG_ICON_FILES);
    MenuAddRow(DESKTOP_ACTION_FILES, L ? L : "Files", 0, 1, 2);
    L = LocStr(MSG_ICON_STORE);
    MenuAddRow(DESKTOP_ACTION_STORE, L ? L : "Store", 0, 1, 3);
    L = LocStr(MSG_ICON_DEVICES);
    MenuAddRow(DESKTOP_ACTION_DEVICES, L ? L : "Devices", 0, 1, 4);
    L = LocStr(MSG_ICON_APPS);
    MenuAddRow(DESKTOP_ACTION_APPS, L ? L : "Apps", 0, 1, 3);
    L = LocStr(MSG_ICON_GAME);
    MenuAddRow(DESKTOP_ACTION_GAME, L ? L : "Game", 0, 1, 3); /* Store 图标；无系统 Snake 槽 */
    FillStartMenuGameRows();

    FillStartMenuAppRows(MenuMaxAppSlots());

    /* 应用组 / 电源组之间不再插空行：Game 与 Shutdown 直接相邻，
       行边框（ThemeMenuSep）即视觉分隔，避免菜单出现无文字无图标的空行。 */

    L = LocStr(MSG_ICON_SHUTDOWN);
    MenuAddRow(DESKTOP_ACTION_SHUTDOWN, L ? L : "Shutdown", 0, 1,
               MENU_ICON_SRC_POWER);
    L = LocStr(MSG_ICON_REBOOT);
    MenuAddRow(DESKTOP_ACTION_REBOOT, L ? L : "Reboot", 0, 1,
               MENU_ICON_SRC_REBOOT);
    /* 预热汉字测宽+绘制路径（裁到空区）；TTF miss 已缓存，勿再三次栅格 */
    {
        int i;

        HalVideoSetClipRegion(0, 0, 0, 0, 0);
        for (i = 0; i < gMenuCount; i++) {
            if (gMenuRows[i].Label[0]) {
                (void)FontStringWidth(gMenuRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuRows[i].Label, 0);
            }
        }
        for (i = 0; i < gMenuAppCount; i++) {
            if (gMenuAppRows[i].Label[0]) {
                (void)FontStringWidth(gMenuAppRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuAppRows[i].Label, 0);
            }
        }
        for (i = 0; i < gMenuGameCount; i++) {
            if (gMenuGameRows[i].Label[0]) {
                (void)FontStringWidth(gMenuGameRows[i].Label);
                HalVideoDrawStringAt(0, 0, gMenuGameRows[i].Label, 0);
            }
        }
        HalVideoClearClip();
    }
}
