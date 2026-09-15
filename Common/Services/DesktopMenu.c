/*
 * DesktopMenu.c — 开始菜单重建（PR-S-desktop-split-3）
 *
 * 从 Desktop.c 迁出；只搬家、不改逻辑。Menu* 辅助多数 static；
 * MenuCopyStr 非 static（DesktopHandleClick 也用）。
 */
#include "DesktopPriv.h"

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

static int MenuAlreadyHasPath(const char *Path) {
    int i;

    for (i = 0; i < gMenuCount; i++) {
        if (gMenuRows[i].Action == DESKTOP_ACTION_EXEC &&
            MenuNameEqIgnoreCase(gMenuRows[i].Path, Path)) {
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
    int Apps;

    TaskbarGeom(&BarY, &Sw, &Sh);
    (void)Sw;
    Room = BarY > 8u ? (BarY - 8u) : 0;
    MaxRows = (int)(Room / MENU_ITEM_H);
    if (MaxRows < MENU_FIXED_TOP + MENU_FIXED_BOT) {
        MaxRows = MENU_FIXED_TOP + MENU_FIXED_BOT;
    }
    if (MaxRows > MENU_ROWS_MAX) {
        MaxRows = MENU_ROWS_MAX;
    }
    Apps = MaxRows - MENU_FIXED_TOP - MENU_FIXED_BOT;
    if (Apps < 0) {
        Apps = 0;
    }
    if (Apps > MENU_APP_MAX) {
        Apps = MENU_APP_MAX;
    }
    return Apps;
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

/* 打开开始菜单时重建：系统项 + Apps/ 下 .ELF + 缺文件的 INST(app) 灰显 */
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
    AppCap = MenuMaxAppSlots();

    L = LocStr(MSG_ICON_SHELL);
    MenuAddRow(DESKTOP_ACTION_SHELL, L ? L : "Shell", 0, 1, 0);
    L = LocStr(MSG_ICON_SETTINGS);
    MenuAddRow(DESKTOP_ACTION_SETTINGS, L ? L : "Settings", 0, 1, 1);
    L = LocStr(MSG_ICON_FILES);
    MenuAddRow(DESKTOP_ACTION_FILES, L ? L : "Files", 0, 1, 2);
    L = LocStr(MSG_ICON_STORE);
    MenuAddRow(DESKTOP_ACTION_STORE, L ? L : "Store", 0, 1, 3);

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
            MenuCopyStr(Path, sizeof(Path), STORE_APPS_DIR);
            /* Apps/ + name */
            {
                int P = 0;
                while (Path[P]) {
                    P++;
                }
                if (P + 1 < (int)sizeof(Path)) {
                    Path[P++] = '/';
                    Path[P] = 0;
                }
                MenuCopyStr(Path + P, (int)sizeof(Path) - P, E->Name);
            }
            MenuLabelFromElf(E->Name, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(E->Name, Label, sizeof(Label));
            MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
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
            MenuCopyStr(Path, sizeof(Path), STORE_APPS_DIR);
            {
                int P = 0;
                while (Path[P]) {
                    P++;
                }
                if (P + 1 < (int)sizeof(Path)) {
                    Path[P++] = '/';
                    Path[P] = 0;
                }
                MenuCopyStr(Path + P, (int)sizeof(Path) - P, In->File);
            }
            if (MenuAlreadyHasPath(Path)) {
                continue;
            }
            if (MenuPathExists(Path)) {
                /* 已在盘上但 list 漏了：仍加一行可开 */
                MenuLabelFromElf(In->File, Label, sizeof(Label));
                MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
                MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
                AppN++;
                continue;
            }
            MenuLabelFromElf(In->File, Label, sizeof(Label));
            MenuEnrichLabelFromCatalog(In->File, Label, sizeof(Label));
            if (!Label[0]) {
                MenuCopyStr(Label, sizeof(Label), In->Id);
            }
            MenuAddRow(DESKTOP_ACTION_EXEC, Label, Path, 0, -1);
            AppN++;
        }
    }

    L = LocStr(MSG_ICON_SHUTDOWN);
    MenuAddRow(DESKTOP_ACTION_SHUTDOWN, L ? L : "Shutdown", 0, 1, 4);
    L = LocStr(MSG_ICON_REBOOT);
    MenuAddRow(DESKTOP_ACTION_REBOOT, L ? L : "Reboot", 0, 1, 5);
}
