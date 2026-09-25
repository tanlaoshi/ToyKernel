/*
 * DesktopMenuApps.c — 开始菜单 Apps 二级（INST taskbar + 旧扁平 ELF）
 */
#include "DesktopPrivate.h"

static int EndsWithElf(const char *Name) {
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

static void LabelFromElf(const char *File, char *Out, int Max) {
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
    if (N >= 4 && EndsWithElf(File)) {
        N -= 4;
    }
    for (i = 0; i < N && i < Max - 1; i++) {
        Out[i] = File[i];
    }
    Out[i] = 0;
}

static int AlreadyHasPath(const char *Path) {
    int i;

    for (i = 0; i < gMenuAppCount; i++) {
        if (gMenuAppRows[i].Action == DESKTOP_ACTION_EXEC &&
            MenuNameEqIgnoreCase(gMenuAppRows[i].Path, Path)) {
            return 1;
        }
    }
    return 0;
}

/* Path 以 /File 或恰好 File 结尾（忽略大小写） */
static int AlreadyHasFile(const char *File) {
    int i;
    int Fn = 0;
    int Pn;

    if (!File || !File[0]) {
        return 0;
    }
    while (File[Fn]) {
        Fn++;
    }
    for (i = 0; i < gMenuAppCount; i++) {
        const char *P = gMenuAppRows[i].Path;

        if (gMenuAppRows[i].Action != DESKTOP_ACTION_EXEC || !P) {
            continue;
        }
        Pn = 0;
        while (P[Pn]) {
            Pn++;
        }
        if (Pn < Fn) {
            continue;
        }
        if (!MenuNameEqIgnoreCase(P + Pn - Fn, File)) {
            continue;
        }
        if (Pn == Fn || P[Pn - Fn - 1] == '/') {
            return 1;
        }
    }
    return 0;
}

static int IconSrcForAppId(const char *Id) {
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

static void EnrichLabelFromCatalog(const char *File, char *Label, int Max) {
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

static void BuildAppsPath(char *Path, int PathMax, const char *File) {
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

static void AddApp(DESKTOP_ACTION Act, const char *Label, const char *Path,
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

/*
 * 先 INST（taskbar=yes / 无 PKG 旧包），再扁平 Apps 下 ELF。
 * 跳过 SNAKE（Game 专用）；扁平与 INST 同名 ELF 去重。
 */
void FillStartMenuAppRows(int AppCap) {
    int AppN = 0;
    int DirN = 0;
    int InstN = 0;
    int i;
    int Err;
    char Path[MENU_PATH_MAX];
    char Label[MENU_LABEL_MAX];

    gMenuAppCount = 0;

    if (StoreListInstalled(gMenuInstScratch, STORE_INSTALLED_MAX, &InstN) == 0) {
        for (i = 0; i < InstN && AppN < AppCap; i++) {
            STORE_INSTALLED *In = &gMenuInstScratch[i];
            STORE_APP_DESKTOP_META Meta;
            int HavePkg;
            int IconSrc;

            if (!(In->Type[0] == 'a' && In->Type[1] == 'p' &&
                  In->Type[2] == 'p' && In->Type[3] == 0)) {
                continue;
            }
            if (!In->File[0]) {
                continue;
            }
            HavePkg = (StoreReadAppDesktopMeta(In->Id, &Meta) == FAT_OK);
            if (HavePkg && !Meta.TaskbarYes) {
                continue;
            }
            IconSrc = IconSrcForAppId(In->Id);
            if (StoreResolveAppPath(In->Id, In->File, Path,
                                    (int)sizeof(Path)) == FAT_OK) {
                if (AlreadyHasPath(Path) || AlreadyHasFile(In->File)) {
                    continue;
                }
                LabelFromElf(In->File, Label, sizeof(Label));
                if (HavePkg && Meta.Title[0]) {
                    MenuCopyStr(Label, sizeof(Label), Meta.Title);
                } else {
                    EnrichLabelFromCatalog(In->File, Label, sizeof(Label));
                }
                AddApp(DESKTOP_ACTION_EXEC, Label, Path, 1, IconSrc);
                AppN++;
                continue;
            }
            if (AlreadyHasFile(In->File)) {
                continue;
            }
            LabelFromElf(In->File, Label, sizeof(Label));
            if (HavePkg && Meta.Title[0]) {
                MenuCopyStr(Label, sizeof(Label), Meta.Title);
            } else {
                EnrichLabelFromCatalog(In->File, Label, sizeof(Label));
            }
            if (!Label[0]) {
                MenuCopyStr(Label, sizeof(Label), In->Id);
            }
            /* 缺文件：灰显 */
            AddApp(DESKTOP_ACTION_EXEC, Label, Path, 0, IconSrc);
            AppN++;
        }
    }

    Err = FileSystemListEntries(STORE_APPS_DIR, gMenuDirScratch, FAT_LIST_MAX,
                                &DirN);
    if (Err != 0 || DirN <= 0) {
        return;
    }
    for (i = 0; i < DirN && AppN < AppCap; i++) {
        const FAT_DIRECTORY_ENTRY *E = &gMenuDirScratch[i];

        if (E->Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (!EndsWithElf(E->Name)) {
            continue;
        }
        /* Game → Snake 专用；勿进 Apps */
        if (MenuNameEqIgnoreCase(E->Name, "SNAKE.ELF")) {
            continue;
        }
        if (AlreadyHasFile(E->Name)) {
            continue;
        }
        BuildAppsPath(Path, (int)sizeof(Path), E->Name);
        if (AlreadyHasPath(Path)) {
            continue;
        }
        LabelFromElf(E->Name, Label, sizeof(Label));
        EnrichLabelFromCatalog(E->Name, Label, sizeof(Label));
        AddApp(DESKTOP_ACTION_EXEC, Label, Path, 1, -1);
        AppN++;
    }
}
