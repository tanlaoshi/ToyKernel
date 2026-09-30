/*
 * ThemeSaveCfg.c — 拼 THEME.CFG 缓冲并写盘（PR-F-theme-1）
 * 核心：ThemeSave.c
 */
#include "ThemePrivate.h"
#include "HalConsole.h"

#define THEME_CFG_CAP 320u

static char sLastCfg[THEME_CFG_CAP];
static UINTN sLastCfgN;

static void BufPut(char *Buf, UINTN *N, UINTN Cap, char C) {
    if (*N + 1 < Cap) {
        Buf[(*N)++] = C;
    }
}

static void BufPutStr(char *Buf, UINTN *N, UINTN Cap, const char *S) {
    int i;

    if (!S) {
        return;
    }
    for (i = 0; S[i]; i++) {
        BufPut(Buf, N, Cap, S[i]);
    }
}

static void BufPutKeyStr(char *Buf, UINTN *N, UINTN Cap, const char *Key,
                         const char *Val) {
    BufPutStr(Buf, N, Cap, Key);
    BufPut(Buf, N, Cap, '=');
    BufPutStr(Buf, N, Cap, Val);
    BufPut(Buf, N, Cap, '\n');
}

static void BufPutKeyHex(char *Buf, UINTN *N, UINTN Cap, const char *Key,
                         UINT32 Color) {
    char Hex[7];

    PutHex6(Hex, Color);
    Hex[6] = 0;
    BufPutKeyStr(Buf, N, Cap, Key, Hex);
}

UINTN ThemeSaveBuildCfg(char *Buf, UINTN Cap, const THEME_SAVE_VALS *V) {
    UINTN N = 0;

    if (!Buf || Cap < 4 || !V) {
        return 0;
    }
    BufPutKeyHex(Buf, &N, Cap, "desktop", gDesktopBg);
    BufPutKeyHex(Buf, &N, Cap, "shell", gShellClientBg);
    BufPutKeyStr(Buf, &N, Cap, "font", V->FontVal);
    if (ThemeHasDisplayPref() && V->ModeVal[0]) {
        BufPutKeyStr(Buf, &N, Cap, "mode", V->ModeVal);
    }
    BufPutKeyStr(Buf, &N, Cap, "scale", V->ScaleVal);
    if (gScaleUserSet) {
        BufPutStr(Buf, &N, Cap, "scalesrc=user\n");
    }
    BufPutKeyStr(Buf, &N, Cap, "fade", V->FadeVal);
    BufPutKeyStr(Buf, &N, Cap, "wallpaper", V->WallVal);
    BufPutKeyStr(Buf, &N, Cap, "theme", ThemeTechName(gThemeId));
    BufPutKeyStr(Buf, &N, Cap, "deskgrad", V->GradVal);
    BufPutKeyStr(Buf, &N, Cap, "theme.effects",
                 ThemeEffectLevelName(gEffectLevel));
    Buf[N] = 0;
    return N;
}

/*
 * 写 THEME.CFG。返回：0 已写；1 内容未变跳过；-1 失败。
 * 勿 Delete 再 Write（vvfat unlink+create 易丢文件）。
 */
int ThemeSaveWriteCfg(const char *Buf, UINTN N) {
    UINTN i;
    int Same;

    if (!Buf || N == 0 || N >= THEME_CFG_CAP) {
        return -1;
    }
    if (N == sLastCfgN && sLastCfgN != 0) {
        Same = 1;
        for (i = 0; i < N; i++) {
            if (sLastCfg[i] != Buf[i]) {
                Same = 0;
                break;
            }
        }
        if (Same) {
            HalConsoleWriteSerial("Theme: already saved (skip)\n");
            return 1;
        }
    }
    if (FileSystemWriteFile(THEME_CFG_PATH, Buf, N) != FAT_OK) {
        HalConsoleWriteSerial("Theme: save THEME.CFG failed\n");
        return -1;
    }
    for (i = 0; i < N && i < sizeof(sLastCfg); i++) {
        sLastCfg[i] = Buf[i];
    }
    sLastCfgN = N;
    return 0;
}
