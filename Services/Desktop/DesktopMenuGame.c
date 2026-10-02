/*
 * DesktopMenuGame.c — 开始菜单 Game 二级（PKG category=game；须经 Store 安装）
 */
#include "DesktopPrivate.h"

static int IconSrcForGameId(const char *Id) {
    int i;

    if (!Id || !Id[0]) {
        return -1;
    }
    for (i = DESKTOP_SYS_ICON_COUNT; i < DESKTOP_ICON_COUNT; i++) {
        if (gIcons[i].Present &&
            MenuNameEqIgnoreCase(gIcons[i].AppId, Id)) {
            return i;
        }
    }
    return -1;
}

static int InstIsGame(const STORE_INSTALLED *In, const STORE_APP_DESKTOP_META *Meta,
                      int HavePkg) {
    if (!In) {
        return 0;
    }
    if (HavePkg && Meta && Meta->CategoryGame) {
        return 1;
    }
    /* 已装旧包未写 category= 时：仅 snake id 兜底（仍须盘上有包） */
    if (MenuNameEqIgnoreCase(In->Id, "snake")) {
        return 1;
    }
    return 0;
}

/*
 * 已装 app 且 category=game → Game flyout（路径 Apps/<id>/…）。
 * 未安装则列表空；不设系统桌面 Snake 捷径。
 */
void FillStartMenuGameRows(void) {
    int InstN = 0;
    int i;

    gMenuGameCount = 0;
    if (StoreListInstalled(gMenuInstScratch, STORE_INSTALLED_MAX, &InstN) != 0) {
        return;
    }
    for (i = 0; i < InstN && gMenuGameCount < MENU_GAME_MAX; i++) {
        STORE_INSTALLED *In = &gMenuInstScratch[i];
        STORE_APP_DESKTOP_META Meta;
        MENU_ROW *Gr;
        char Path[MENU_PATH_MAX];
        int HavePkg;
        const char *L;

        if (!(In->Type[0] == 'a' && In->Type[1] == 'p' &&
              In->Type[2] == 'p' && In->Type[3] == 0)) {
            continue;
        }
        if (!In->File[0]) {
            continue;
        }
        HavePkg = (StoreReadAppDesktopMeta(In->Id, &Meta) == FAT_OK);
        if (!InstIsGame(In, &Meta, HavePkg)) {
            continue;
        }
        if (HavePkg && !Meta.TaskbarYes) {
            continue;
        }
        if (StoreResolveAppPath(In->Id, In->File, Path,
                                (int)sizeof(Path)) != FAT_OK) {
            continue;
        }
        Gr = &gMenuGameRows[gMenuGameCount++];
        Gr->Action = DESKTOP_ACTION_EXEC;
        Gr->Enabled = 1;
        Gr->IconSrc = IconSrcForGameId(In->Id);
        if (HavePkg && Meta.Title[0]) {
            MenuCopyStr(Gr->Label, sizeof(Gr->Label), Meta.Title);
        } else if (MenuNameEqIgnoreCase(In->Id, "snake")) {
            L = LocStr(MSG_ICON_SNAKE);
            MenuCopyStr(Gr->Label, sizeof(Gr->Label), L ? L : "Snake");
        } else {
            MenuCopyStr(Gr->Label, sizeof(Gr->Label), In->Id);
        }
        MenuCopyStr(Gr->Path, sizeof(Gr->Path), Path);
    }
}
