/*
 * StorePkgMeta.c — PR-S-bundle-desktop：读 Apps/<id>/PKG.TXT 桌面相关键
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"

#define PKG_BUF_MAX 2048

static int IsYesToken(const char *Val) {
    if (!Val || !Val[0]) {
        return 0;
    }
    if (Val[0] == '1' && Val[1] == 0) {
        return 1;
    }
    if ((Val[0] == 'y' || Val[0] == 'Y') &&
        (Val[1] == 'e' || Val[1] == 'E') &&
        (Val[2] == 's' || Val[2] == 'S') && Val[3] == 0) {
        return 1;
    }
    return 0;
}

static void ApplyPkgLine(STORE_APP_DESKTOP_META *Out, const char *Key,
                         const char *Val) {
    if (!Out || !Key) {
        return;
    }
    if (StrEqIgnoreCase(Key, "desktop")) {
        Out->DesktopYes = IsYesToken(Val);
    } else if (StrEqIgnoreCase(Key, "taskbar")) {
        Out->TaskbarYes = IsYesToken(Val);
    } else if (StrEqIgnoreCase(Key, "title") && Val) {
        CopyStr(Out->Title, (int)sizeof(Out->Title), Val);
    } else if (StrEqIgnoreCase(Key, "icon") && Val) {
        CopyStr(Out->IconRel, (int)sizeof(Out->IconRel), Val);
    }
}

static void ParsePkgBuf(const char *Buf, UINTN Size, STORE_APP_DESKTOP_META *Out) {
    UINTN i = 0;

    while (i < Size) {
        char Key[32];
        char Val[96];
        int Ki = 0;
        int Vi = 0;

        while (i < Size && (Buf[i] == '\r' || Buf[i] == '\n' || Buf[i] == ' ')) {
            i++;
        }
        if (i >= Size) {
            break;
        }
        if (Buf[i] == '#') {
            while (i < Size && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        while (i < Size && Buf[i] != '=' && Buf[i] != '\n' && Ki + 1 < (int)sizeof(Key)) {
            if (Buf[i] != '\r') {
                Key[Ki++] = Buf[i];
            }
            i++;
        }
        Key[Ki] = 0;
        if (i >= Size || Buf[i] != '=') {
            while (i < Size && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        i++;
        while (i < Size && Buf[i] != '\n' && Vi + 1 < (int)sizeof(Val)) {
            if (Buf[i] != '\r') {
                Val[Vi++] = Buf[i];
            }
            i++;
        }
        Val[Vi] = 0;
        ApplyPkgLine(Out, Key, Val);
        if (i < Size && Buf[i] == '\n') {
            i++;
        }
    }
}

int StoreReadAppDesktopMeta(const char *Id, STORE_APP_DESKTOP_META *Out) {
    char Path[128];
    char Buf[PKG_BUF_MAX];
    UINTN Size = 0;
    int Err;

    if (!Id || !Id[0] || !Out) {
        return FAT_ERR_INVAL;
    }
    Out->DesktopYes = 0;
    Out->TaskbarYes = 0;
    Out->Title[0] = 0;
    Out->IconRel[0] = 0;

    StoreAppBundleDir(Path, (int)sizeof(Path), Id);
    JoinPath(Path, (int)sizeof(Path), Path, "PKG.TXT");
    Err = FileSystemReadFile(Path, Buf, sizeof(Buf) - 1, &Size);
    if (Err != FAT_OK) {
        return Err;
    }
    if (Size >= sizeof(Buf)) {
        Size = sizeof(Buf) - 1;
    }
    Buf[Size] = 0;
    ParsePkgBuf(Buf, Size, Out);
    return FAT_OK;
}
