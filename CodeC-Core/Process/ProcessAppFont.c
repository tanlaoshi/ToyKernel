/*
 * ProcessAppFont.c — PR-S-app-font：exec 时按 PKG font= 挂/卸应用私有字
 *
 * 单用户 GUI 下切全局当前字即可；无 font= 或非 Apps/<id>/ 路径则保持系统字。
 */
#include "ProcessPrivate.h"
#include "Store.h"
#include "StorePrivate.h"
#include "Font.h"
#include "Fat.h"
#include "Debug.h"

static UINT32 gSavedFontId;
static int gAppFontHeld;

static int EqCiPrefix(const char *S, const char *Pfx) {
    while (*Pfx) {
        char A = *S;
        char B = *Pfx;

        if (A >= 'A' && A <= 'Z') {
            A = (char)(A - 'A' + 'a');
        }
        if (B >= 'A' && B <= 'Z') {
            B = (char)(B - 'A' + 'a');
        }
        if (A != B) {
            return 0;
        }
        S++;
        Pfx++;
    }
    return 1;
}

/* Path = Apps/<id>/... → Id；失败 -1 */
static int ExtractAppsId(const char *Path, char *Id, int IdMax) {
    const char *P = Path;
    int i;

    if (!Path || !Id || IdMax < 2) {
        return -1;
    }
    while (*P == '/' || *P == '\\') {
        P++;
    }
    if (!EqCiPrefix(P, "Apps/")) {
        return -1;
    }
    P += 5;
    i = 0;
    while (*P && *P != '/' && *P != '\\' && i + 1 < IdMax) {
        Id[i++] = *P++;
    }
    Id[i] = 0;
    if (i == 0 || (*P != '/' && *P != '\\')) {
        return -1;
    }
    return 0;
}

void ProcessRestoreAppFont(void) {
    if (!gAppFontHeld) {
        return;
    }
    FontUnloadApp();
    if (FontSetById(gSavedFontId) != 0 && FontCount() > 0) {
        (void)FontSetById(0);
    }
    gAppFontHeld = 0;
    DebugWrite("process: app font restored\n");
}

void ProcessApplyAppFont(const char *Path) {
    STORE_APP_DESKTOP_META Meta;
    char Id[STORE_ID_MAX];
    char FontPath[128];
    int Fid;

    ProcessRestoreAppFont();
    if (!Path || ExtractAppsId(Path, Id, (int)sizeof(Id)) != 0) {
        return;
    }
    if (StoreReadAppDesktopMeta(Id, &Meta) != FAT_OK || !Meta.FontRel[0]) {
        return;
    }

    StoreAppBundleDir(FontPath, (int)sizeof(FontPath), Id);
    JoinPath(FontPath, (int)sizeof(FontPath), FontPath, Meta.FontRel);

    Fid = FontLoadPath(FontPath);
    if (Fid < 0) {
        DebugWrite("process: app font load fail ");
        DebugWrite(FontPath);
        DebugWrite("\n");
        return;
    }

    gSavedFontId = FontCurrentId();
    if (FontSetById((UINT32)Fid) != 0) {
        FontUnloadApp();
        DebugWrite("process: app font set fail\n");
        return;
    }
    gAppFontHeld = 1;
    DebugWrite("process: app font ");
    DebugWrite(FontPath);
    DebugWrite(" id=");
    DebugHex32((UINT32)Fid);
    DebugWrite("\n");
}
