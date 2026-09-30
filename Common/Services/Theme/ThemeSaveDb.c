/*
 * ThemeSaveDb.c — 批量写 TOYOS.DB（PR-F-theme-1）
 * 核心：ThemeSave.c
 */
#include "ThemePrivate.h"
#include "HalConsole.h"

/* 0=全成；-1=有失败（CFG 已写时仍只打日志） */
int ThemeSaveWriteDb(const THEME_SAVE_VALS *V) {
    char Hex[7];
    int DbOk = 1;

    if (!V) {
        return -1;
    }
    DbBeginBatch();
    PutHex6(Hex, gDesktopBg);
    Hex[6] = 0;
    if (DbSet("desktop", Hex) != DB_OK) {
        DbOk = 0;
    }
    PutHex6(Hex, gShellClientBg);
    if (DbSet("shell", Hex) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("font", V->FontVal) != DB_OK) {
        DbOk = 0;
    }
    if (ThemeHasDisplayPref() && V->ModeVal[0]) {
        if (DbSet("mode", V->ModeVal) != DB_OK) {
            DbOk = 0;
        }
    } else {
        (void)DbDelete("mode");
    }
    if (DbSet("scale", V->ScaleVal) != DB_OK) {
        DbOk = 0;
    }
    if (gScaleUserSet) {
        if (DbSet("scalesrc", "user") != DB_OK) {
            DbOk = 0;
        }
    } else {
        (void)DbDelete("scalesrc");
    }
    if (DbSet("fade", V->FadeVal) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("wallpaper", V->WallVal) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("theme", ThemeTechName(gThemeId)) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("deskgrad", V->GradVal) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("theme.effects", ThemeEffectLevelName(gEffectLevel)) != DB_OK) {
        DbOk = 0;
    }
    if (DbEndBatch() != DB_OK) {
        DbOk = 0;
    }
    if (!DbOk) {
        HalConsoleWriteSerial("Theme: saved THEME.CFG (DB write failed)\n");
        return -1;
    }
    HalConsoleWriteSerial("Theme: saved THEME.CFG + TOYOS.DB\n");
    return 0;
}
