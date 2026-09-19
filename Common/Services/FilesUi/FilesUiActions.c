/*
 * FilesUiActions.c — Files 打开/删除/新建/改名（PR-S-filesui-split-3）
 *
 * 从 FilesUi.c 迁出 Actions；只搬家、不改逻辑。
 */
#include "FilesUiPriv.h"
#include "Store.h"
#include "Fat.h"

void OpenSelected(void) {
    FAT_DIRECTORY_ENTRY *E;
    char Path[FILES_PATH_MAX];

    if (gMode != FILES_MODE_LIST || gSelected < 0 || gSelected >= gCount) {
        return;
    }
    E = &gEnts[gSelected];
    if (E->Attr & FAT_ATTR_DIR) {
        if (E->Name[0] == '.' && E->Name[1] == 0) {
            return;
        }
        if (E->Name[0] == '.' && E->Name[1] == '.' && E->Name[2] == 0) {
            CwdPop();
        } else {
            if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
                return;
            }
            CopyStr(gCwd, sizeof(gCwd), Path);
        }
        gMode = FILES_MODE_LIST;
        SetStatus("");
        (void)ReloadList();
        Paint();
        return;
    }

    if (!JoinPath(Path, sizeof(Path), gCwd, E->Name)) {
        return;
    }
    if (EndsWithElf(E->Name)) {
        if (ProcessExec(Path) != 0) {
            SetStatus("exec failed");
            Paint();
        }
        return;
    }

    gViewLen = 0;
    if (FileSystemReadFile(Path, gView, sizeof(gView) - 1, &gViewLen) != FAT_OK) {
        SetStatus("read failed");
        Paint();
        return;
    }
    gView[gViewLen] = 0;
    CopyStr(gViewTitle, sizeof(gViewTitle), E->Name);
    if (IsMostlyText(gView, gViewLen)) {
        if (GuiOpenEdit(Path) < 0) {
            SetStatus("edit: no free window");
            Paint();
        }
        return;
    }
    gMode = FILES_MODE_VIEW;
    Paint();
}

void BeginConfirmDelete(void) {
    char Path[FILES_PATH_MAX];

    if (gMode != FILES_MODE_LIST || gSelected < 0 || gSelected >= gCount) {
        return;
    }
    if (gEnts[gSelected].Name[0] == '.' &&
        (gEnts[gSelected].Name[1] == 0 ||
         (gEnts[gSelected].Name[1] == '.' && gEnts[gSelected].Name[2] == 0))) {
        SetStatus("cannot delete . / ..");
        Paint();
        return;
    }
    if (!JoinPath(Path, sizeof(Path), gCwd, gEnts[gSelected].Name)) {
        SetStatus("bad path");
        Paint();
        return;
    }
    /* 商店托管 ELF：直接提示，不进确认框 */
    if (!(gEnts[gSelected].Attr & FAT_ATTR_DIR) && StoreIsManagedPayload(Path)) {
        SetStatus(LocStr(MSG_FILES_STORE_MANAGED));
        Paint();
        return;
    }
    gMode = FILES_MODE_CONFIRM;
    Paint();
}

void BeginPrompt(FILES_PROMPT_KIND Kind) {
    char Path[FILES_PATH_MAX];

    if (gMode != FILES_MODE_LIST) {
        return;
    }
    if (Kind == FILES_PROMPT_RENAME) {
        if (gSelected < 0 || gSelected >= gCount) {
            return;
        }
        if (gEnts[gSelected].Name[0] == '.' &&
            (gEnts[gSelected].Name[1] == 0 ||
             (gEnts[gSelected].Name[1] == '.' && gEnts[gSelected].Name[2] == 0))) {
            SetStatus("cannot rename . / ..");
            Paint();
            return;
        }
        if (JoinPath(Path, sizeof(Path), gCwd, gEnts[gSelected].Name) &&
            StoreIsManagedPayload(Path)) {
            SetStatus(LocStr(MSG_FILES_STORE_MANAGED));
            Paint();
            return;
        }
    }
    gPromptKind = Kind;
    gPromptLen = 0;
    gPrompt[0] = 0;
    gMode = FILES_MODE_PROMPT;
    Paint();
}

void DoDelete(void) {
    char Path[FILES_PATH_MAX];
    int Err;
    int IsDir;

    if (gSelected < 0 || gSelected >= gCount) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    IsDir = (gEnts[gSelected].Attr & FAT_ATTR_DIR) != 0;
    if (!JoinPath(Path, sizeof(Path), gCwd, gEnts[gSelected].Name)) {
        SetStatus("bad path");
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    Err = IsDir ? FileSystemRemoveDirectory(Path) : FileSystemDeleteFile(Path);
    gMode = FILES_MODE_LIST;
    if (Err == FAT_ERR_STORE) {
        SetStatus(LocStr(MSG_FILES_STORE_MANAGED));
    } else if (Err != FAT_OK) {
        SetStatus(FatStrError(Err));
    } else {
        SetStatus("deleted");
    }
    (void)ReloadList();
    Paint();
}

void DoPromptCommit(void) {
    char Path[FILES_PATH_MAX];
    char OldPath[FILES_PATH_MAX];
    int Err = FAT_OK;

    gPrompt[gPromptLen] = 0;
    if (!NameOk(gPrompt)) {
        SetStatus("bad name");
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }
    if (!JoinPath(Path, sizeof(Path), gCwd, gPrompt)) {
        gMode = FILES_MODE_LIST;
        Paint();
        return;
    }

    if (gPromptKind == FILES_PROMPT_MKDIR) {
        Err = FileSystemMakeDirectory(Path);
        SetStatus(Err == FAT_OK ? "mkdir ok" : FatStrError(Err));
    } else if (gPromptKind == FILES_PROMPT_NEWFILE) {
        /* vvfat：0 字节文件常不落宿主盘，重开即消失；写 1 字节换行可持久化 */
        Err = FileSystemWriteFile(Path, "\n", 1);
        SetStatus(Err == FAT_OK ? "file ok" : FatStrError(Err));
    } else {
        if (!JoinPath(OldPath, sizeof(OldPath), gCwd, gEnts[gSelected].Name)) {
            gMode = FILES_MODE_LIST;
            Paint();
            return;
        }
        Err = FileSystemRename(OldPath, Path);
        if (Err == FAT_ERR_STORE) {
            SetStatus(LocStr(MSG_FILES_STORE_MANAGED));
        } else {
            SetStatus(Err == FAT_OK ? "renamed" : FatStrError(Err));
        }
    }
    gMode = FILES_MODE_LIST;
    (void)ReloadList();
    Paint();
}

