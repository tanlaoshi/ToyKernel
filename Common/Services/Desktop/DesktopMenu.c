/*
 * DesktopMenu.c — 开始菜单重建（PR-S-desktop-split-3）
 *
 * 系统项 + Apps 一级；已装 ELF / INST 灰显进二级 flyout。
 */
#include "DesktopPrivate.h"

/* 亦被 DesktopHandleClick 调用，不可 static */
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

static int MenuNameEqIgnoreCase(const char *A, const char *B) {
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

static int MenuEndsWithElf(const char *Name) {
    int N = 0;

    if (!Name) {
        return 0;
    }
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    return MenuNameEqIgnoreCase(Name + N - 4, ".elf");
}

static void MenuLabelFromElf(const char *File, char *Out, int Max) {
    int i;
    int N = 0;

    if (!Out || Max <= 0) {
        return;
    }
    if (!File) {
        Out[0] = 0;
        return;
    }
    while (File[N]) {
        N++;
    }
    if (N >= 4 && MenuEndsWithElf(File)) {
        N -= 4;
    }
    for (i = 0; i < N && i < Max - 1; i++) {
        Out[i] = File[i];
    }
    Out[i] = 0;
}

static int MenuPathExists(const char *Path) {
    FAT_FILE_STAT St;

    if (!Path || !Path[0]) {
        return 0;
    }
    return FileSystemFileStat(Path, &St) == 0;
}

static int MenuAlreadyHasAppPath(const char *Path) {
    int i;

    for (i = 0; i < gMenuAppCount; i++) {
        if (gMenuAppRows[i].Action == DESKTOP_ACTION_EXEC &&
            MenuNameEqIgnoreCase(gMenuAppRows[i].Path, Path)) {
            return 1;
        }
    }
    return 0;
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

static void MenuAddAppRow(DESKTOP_ACTION Act, const char *Label, const char *Path,
                          int Enabled, int IconSrc) {
    MENU_ROW *R;

    if (gMenuAppCount >= MENU_APP_MAX) {
        return;
    }
    R = &gMenuAppRows[gMenuAppCount++];
    R->Action = Act;
    R->Enabled = Enabled ? 1 : 0;
    R->IconSrc = IconSrc;
    MenuCopyStr(R->Label, sizeof(R->Label), Label ? Label : "");
    MenuCopyStr(R->Path, sizeof(R->Path), Path ? Path : "");
}

static void MenuEnrichLabelFromCatalog(const char *File, char *Label, int Max) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;

    if (!File || !Label || Max <= 0) {
        return;
    }
    Tab = StoreScratchTab();
    if (!Tab) {
        return;
    }
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) < 0 || Count <= 0) {
        return;
    }
    for (i = 0; i < Count; i++) {
        if (Tab[i].Type[0] == 'a' &&
            MenuNameEqIgnoreCase(Tab[i].File, File) &&
            Tab[i].Title[0]) {
            MenuCopyStr(Label, Max, Tab[i].Title);
            return;
        }
    }
}

static void MenuBuildAppsPath(char *Path, int PathMax, const char *File) {
    int P = 0;

    MenuCopyStr(Path, PathMax, STORE_APPS_DIR);
    while (Path[P]) {
        P++;
    }
    if (P + 1 < PathMax) {
        Path[P++] = '/';
        Path[P] = 0;
    }
    MenuCopyStr(Path + P, PathMax - P, File);
}

/* 打开开始菜单时重建：系统项含 Apps 一级；ELF/INST → 二级 */
void RebuildStartMenu(void) {
    int AppCap;
    int AppN = 0;
    int DirN = 0;
    int InstN = 0;
    int i;
    int Err;
    char Path[MENU_PATH_MAX];
    char Label[MENU_LABEL_MAX];
    const char *L;

    gMenuCount = 0;
    gMenuAppCount = 0;
    gMenuGameCount = 0;
    AppCap = MenuMaxAppSlots();

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
    MenuAddRow(DESKTOP_ACTION_APPS, L ? L : "Apps", 0, 1, 2);
    L = LocStr(MSG_ICON_GAME);
    MenuAddRow(DESKTOP_ACTION_GAME, L ? L : "Game", 0, 1, 5);
    if (gMenuGameCount < MENU_GAME_MAX) {
        MENU_ROW *Gr = &gMenuGameRows[gMenuGameCount++];

        L = LocStr(MSG_ICON_SNAKE);
        Gr->Action = DESKTOP_ACTION_EXEC;
        Gr->Enabled = 1;
        Gr->IconSrc = 5;
        MenuCopyStr(Gr->Label, sizeof(Gr->Label), L ? L : "Snake");
        MenuCopyStr(Gr->Path, sizeof(Gr->Path), "SNAKE.ELF");
    }

    Err = FileSystemListEntries(STORE_APPS_DIR, gMenuDirScratch, FAT_LIST_MAX,
                                &DirN);
    if (Err == 0 && DirN > 0) {
        for (i = 0; i < DirN && AppN < AppCap; i++) {
            const FAT_DIRECTORY_ENTRY *E = &gMenuDirScratch[i];

            if (E->Attr & FAT_ATTR_DIR) {
                continue;
            }
            if (!MenuEndsWithElf(E->Name)) {
                continue;
            }
            MenuBuildAppsPath(Path, (int)sizeof(Path), E->Name);
            MenuLabelFromElf(E->Name, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(E->Name, Label, sizeof(Label));
            MenuAddAppRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
            AppN++;
        }
    }

    /* INST app 但 Apps/ 无文件 → 灰显（可看见、不可开） */
    if (StoreListInstalled(gMenuInstScratch, STORE_INSTALLED_MAX, &InstN) == 0) {
        for (i = 0; i < InstN && AppN < AppCap; i++) {
            STORE_INSTALLED *In = &gMenuInstScratch[i];

            if (!(In->Type[0] == 'a' && In->Type[1] == 'p' &&
                  In->Type[2] == 'p' && In->Type[3] == 0)) {
                continue;
            }
            if (!In->File[0]) {
                continue;
            }
            MenuBuildAppsPath(Path, (int)sizeof(Path), In->File);
            if (MenuAlreadyHasAppPath(Path)) {
                continue;
            }
            if (MenuPathExists(Path)) {
                MenuLabelFromElf(In->File, Label, sizeof(Label));
                MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
                MenuAddAppRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
                AppN++;
                continue;
            }
            MenuLabelFromElf(In->File, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
            if (!Label[0]) {
                MenuCopyStr(Label, sizeof(Label), In->Id);
            }
            MenuAddAppRow(DESKTOP_ACTION_EXEC, Label, Path, 0, -1);
            AppN++;
        }
    }

    L = LocStr(MSG_ICON_SHUTDOWN);
    MenuAddRow(DESKTOP_ACTION_SHUTDOWN, L ? L : "Shutdown", 0, 1, 6);
    L = LocStr(MSG_ICON_REBOOT);
    MenuAddRow(DESKTOP_ACTION_REBOOT, L ? L : "Reboot", 0, 1, 7);
}
