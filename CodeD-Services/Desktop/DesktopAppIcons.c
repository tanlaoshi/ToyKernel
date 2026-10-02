/*
 * DesktopAppIcons.c — PR-S-bundle-desktop：desktop=yes 动态桌面图标
 */
#include "DesktopPrivate.h"

#define DEFAULT_APP_ICON "Assets/Icons/bmp48/HOME.BMP"

static void ClearAppSlots(void) {
    int i;

    for (i = DESKTOP_SYS_ICON_COUNT; i < DESKTOP_ICON_COUNT; i++) {
        if (gIcons[i].BmpReady) {
            BmpFree(&gIcons[i].Bmp);
        }
        gIcons[i].Present = 0;
        gIcons[i].BmpReady = 0;
        gIcons[i].Label = 0;
        gIcons[i].BmpPath = 0;
        gIcons[i].ExecPath = 0;
        gIcons[i].Action = DESKTOP_ACTION_NONE;
        gIcons[i].AppId[0] = 0;
        gIcons[i].LabelBuf[0] = 0;
        gIcons[i].BmpPathBuf[0] = 0;
        gIcons[i].ExecPathBuf[0] = 0;
    }
}

static void BuildIconPath(char *Out, int Max, const char *Id,
                          const char *IconRel) {
    int P = 0;

    if (!Out || Max <= 0) {
        return;
    }
    Out[0] = 0;
    if (!IconRel || !IconRel[0]) {
        MenuCopyStr(Out, Max, DEFAULT_APP_ICON);
        return;
    }
    MenuCopyStr(Out, Max, STORE_APPS_DIR);
    while (Out[P]) {
        P++;
    }
    if (P + 1 < Max) {
        Out[P++] = '/';
        Out[P] = 0;
    }
    MenuCopyStr(Out + P, Max - P, Id ? Id : "");
    while (Out[P]) {
        P++;
    }
    if (P + 1 < Max) {
        Out[P++] = '/';
        Out[P] = 0;
    }
    MenuCopyStr(Out + P, Max - P, IconRel);
}

void DesktopLoadAppIcons(void) {
    STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
    int InstN = 0;
    int Slot = DESKTOP_SYS_ICON_COUNT;
    int i;
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;
    UINT32 ColX = DESKTOP_ORIGIN_X + DESKTOP_ICON_SIZE + DESKTOP_ICON_GAP + 24u;
    int Row = 0;

    ClearAppSlots();
    if (StoreListInstalled(Inst, STORE_INSTALLED_MAX, &InstN) != FAT_OK) {
        return;
    }
    for (i = 0; i < InstN && Slot < DESKTOP_ICON_COUNT; i++) {
        STORE_APP_DESKTOP_META Meta;
        DESKTOP_ICON *Icon;
        char Exec[MENU_PATH_MAX];

        if (!(Inst[i].Type[0] == 'a' && Inst[i].Type[1] == 'p' &&
              Inst[i].Type[2] == 'p' && Inst[i].Type[3] == 0)) {
            continue;
        }
        if (StoreReadAppDesktopMeta(Inst[i].Id, &Meta) != FAT_OK) {
            continue;
        }
        if (!Meta.DesktopYes) {
            continue;
        }
        if (StoreResolveAppPath(Inst[i].Id, Inst[i].File, Exec,
                                (int)sizeof(Exec)) != FAT_OK) {
            continue;
        }

        Icon = &gIcons[Slot];
        MenuCopyStr(Icon->AppId, (int)sizeof(Icon->AppId), Inst[i].Id);
        if (Meta.Title[0]) {
            MenuCopyStr(Icon->LabelBuf, (int)sizeof(Icon->LabelBuf), Meta.Title);
        } else {
            MenuCopyStr(Icon->LabelBuf, (int)sizeof(Icon->LabelBuf), Inst[i].Id);
        }
        BuildIconPath(Icon->BmpPathBuf, (int)sizeof(Icon->BmpPathBuf),
                      Inst[i].Id, Meta.IconRel);
        MenuCopyStr(Icon->ExecPathBuf, (int)sizeof(Icon->ExecPathBuf), Exec);

        Icon->Label = Icon->LabelBuf;
        Icon->BmpPath = Icon->BmpPathBuf;
        Icon->ExecPath = Icon->ExecPathBuf;
        Icon->Action = DESKTOP_ACTION_EXEC;
        Icon->IconColor = 0x0060A0C0;
        Icon->Present = 1;
        Icon->X = ColX;
        Icon->Y = DESKTOP_ORIGIN_Y + (UINT32)Row * RowH;
        ClampIconPos(&Icon->X, &Icon->Y);
        SnapIconToGrid(&Icon->X, &Icon->Y);
        Slot++;
        Row++;
    }
    if (Slot > DESKTOP_SYS_ICON_COUNT) {
        DebugWrite("desktop: app icons from PKG desktop=yes\n");
    }
}
