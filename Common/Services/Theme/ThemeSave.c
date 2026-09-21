/*
 * ThemeSave.c — 写 THEME.CFG 与 TOYOS.DB
 * 核心：Theme.c；格式化：ThemeSaveFmt.c
 */
#include "Theme.h"
#include "ThemePrivate.h"
#include "HalConsole.h"

int ThemeSave(void) {
    char Buf[320];
    UINTN N = 0;
    char Hex[7];
    char FontVal[8];
    char ModeVal[24];
    char ScaleVal[8];
    char FadeVal[8];
    char WallVal[2];
    char GradVal[2];
    UINTN ModeLen = 0;
    UINTN ScaleLen = 0;
    UINTN FadeLen = 0;
    int i;
    int DbOk = 1;
    static char sLastCfg[320];
    static UINTN sLastCfgN;
    static int sBusy;

    if (sBusy) {
        HalConsoleWriteSerial("Theme: save reenter skipped\n");
        return -1;
    }
    sBusy = 1;

    FontVal[0] = 0;
    N = 0;
    if (gFontId >= 10) {
        FontVal[N++] = (char)('0' + (gFontId / 10) % 10);
    }
    FontVal[N++] = (char)('0' + (gFontId % 10));
    FontVal[N] = 0;
    if (ThemeHasDisplayPref()) {
        ModeLen = 0;
        PutDec(ModeVal, gModeW, &ModeLen);
        ModeVal[ModeLen++] = 'x';
        PutDec(ModeVal, gModeH, &ModeLen);
        ModeVal[ModeLen] = 0;
    }
    ScaleLen = 0;
    PutDec(ScaleVal, ThemeUiScale(), &ScaleLen);
    ScaleVal[ScaleLen] = 0;
    FadeLen = 0;
    PutDec(FadeVal, ThemeWindowFadeSteps(), &FadeLen);
    FadeVal[FadeLen] = 0;
    WallVal[0] = gWallpaper ? '1' : '0';
    WallVal[1] = 0;
    GradVal[0] = gDesktopGrad ? '1' : '0';
    GradVal[1] = 0;

    /*
     * 先写 THEME.CFG：QEMU edid / ToyBoot 认 CFG；若先写 DB 再 CFG 失败，
     * Settings 显示新分辨率、下次启动仍用旧 edid（常见「设了却变回 1600x900」）。
     */
    N = 0;
    Buf[N++] = 'd';
    Buf[N++] = 'e';
    Buf[N++] = 's';
    Buf[N++] = 'k';
    Buf[N++] = 't';
    Buf[N++] = 'o';
    Buf[N++] = 'p';
    Buf[N++] = '=';
    PutHex6(Hex, gDesktopBg);
    Hex[6] = 0;
    for (i = 0; Hex[i]; i++) {
        Buf[N++] = Hex[i];
    }
    Buf[N++] = '\n';

    Buf[N++] = 's';
    Buf[N++] = 'h';
    Buf[N++] = 'e';
    Buf[N++] = 'l';
    Buf[N++] = 'l';
    Buf[N++] = '=';
    PutHex6(Hex, gShellClientBg);
    for (i = 0; Hex[i]; i++) {
        Buf[N++] = Hex[i];
    }
    Buf[N++] = '\n';

    Buf[N++] = 'f';
    Buf[N++] = 'o';
    Buf[N++] = 'n';
    Buf[N++] = 't';
    Buf[N++] = '=';
    for (i = 0; FontVal[i]; i++) {
        Buf[N++] = FontVal[i];
    }
    Buf[N++] = '\n';

    if (ThemeHasDisplayPref()) {
        Buf[N++] = 'm';
        Buf[N++] = 'o';
        Buf[N++] = 'd';
        Buf[N++] = 'e';
        Buf[N++] = '=';
        for (i = 0; ModeVal[i]; i++) {
            Buf[N++] = ModeVal[i];
        }
        Buf[N++] = '\n';
    }
    Buf[N++] = 's';
    Buf[N++] = 'c';
    Buf[N++] = 'a';
    Buf[N++] = 'l';
    Buf[N++] = 'e';
    Buf[N++] = '=';
    for (i = 0; ScaleVal[i]; i++) {
        Buf[N++] = ScaleVal[i];
    }
    Buf[N++] = '\n';
    if (gScaleUserSet) {
        const char *Src = "scalesrc=user\n";
        for (i = 0; Src[i]; i++) {
            Buf[N++] = Src[i];
        }
    }
    Buf[N++] = 'f';
    Buf[N++] = 'a';
    Buf[N++] = 'd';
    Buf[N++] = 'e';
    Buf[N++] = '=';
    for (i = 0; FadeVal[i]; i++) {
        Buf[N++] = FadeVal[i];
    }
    Buf[N++] = '\n';
    Buf[N++] = 'w';
    Buf[N++] = 'a';
    Buf[N++] = 'l';
    Buf[N++] = 'l';
    Buf[N++] = 'p';
    Buf[N++] = 'a';
    Buf[N++] = 'p';
    Buf[N++] = 'e';
    Buf[N++] = 'r';
    Buf[N++] = '=';
    Buf[N++] = WallVal[0];
    Buf[N++] = '\n';
    {
        const char *Tn = ThemeTechName(gThemeId);
        Buf[N++] = 't';
        Buf[N++] = 'h';
        Buf[N++] = 'e';
        Buf[N++] = 'm';
        Buf[N++] = 'e';
        Buf[N++] = '=';
        for (i = 0; Tn[i]; i++) {
            Buf[N++] = Tn[i];
        }
        Buf[N++] = '\n';
    }
    Buf[N++] = 'd';
    Buf[N++] = 'e';
    Buf[N++] = 's';
    Buf[N++] = 'k';
    Buf[N++] = 'g';
    Buf[N++] = 'r';
    Buf[N++] = 'a';
    Buf[N++] = 'd';
    Buf[N++] = '=';
    Buf[N++] = GradVal[0];
    Buf[N++] = '\n';
    {
        const char *En = ThemeEffectLevelName(gEffectLevel);
        const char *Ek = "theme.effects=";
        for (i = 0; Ek[i]; i++) {
            Buf[N++] = Ek[i];
        }
        for (i = 0; En[i]; i++) {
            Buf[N++] = En[i];
        }
        Buf[N++] = '\n';
    }
    Buf[N] = 0;
    /*
     * 勿先 Delete 再 Write：QEMU fat:rw/vvfat 上 unlink+create 常丢宿主文件
     * 或整机异常退出（Settings 改分辨率「saved」但盘上仍是旧 mode）。
     * FatWriteFile 已支持同名覆盖。
     * 内容未变则跳过 CFG 写（连点 Settings 时减轻 vvfat commit）。
     */
    if (N != sLastCfgN || sLastCfgN == 0) {
        /* fall through write */
    } else {
        int Same = 1;
        for (i = 0; (UINTN)i < N; i++) {
            if (sLastCfg[i] != Buf[i]) {
                Same = 0;
                break;
            }
        }
        if (Same) {
            HalConsoleWriteSerial("Theme: already saved (skip)\n");
            sBusy = 0;
            return 0;
        }
    }

    if (FileSystemWriteFile(THEME_CFG_PATH, Buf, N) != FAT_OK) {
        HalConsoleWriteSerial("Theme: save THEME.CFG failed\n");
        sBusy = 0;
        return -1;
    }
    for (i = 0; (UINTN)i < N && (UINTN)i < sizeof(sLastCfg); i++) {
        sLastCfg[i] = Buf[i];
    }
    sLastCfgN = N;
    /*
     * 故意不在 Write 后立刻 Read 同文件：QEMU fat:rw/vvfat 会断言
     * get_cluster_count_for_direntry（Settings 改分辨率整机 abort）。
     * 宿主可用 cat rootfs/THEME.CFG 确认；Guest 以内存 + DB 为准。
     */

    /* 多次 DbSet 合并一次刷盘，减轻 vvfat 连写压力 */
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
    if (DbSet("font", FontVal) != DB_OK) {
        DbOk = 0;
    }
    if (ThemeHasDisplayPref()) {
        if (DbSet("mode", ModeVal) != DB_OK) {
            DbOk = 0;
        }
    } else {
        (void)DbDelete("mode");
    }
    if (DbSet("scale", ScaleVal) != DB_OK) {
        DbOk = 0;
    }
    if (gScaleUserSet) {
        if (DbSet("scalesrc", "user") != DB_OK) {
            DbOk = 0;
        }
    } else {
        (void)DbDelete("scalesrc");
    }
    if (DbSet("fade", FadeVal) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("wallpaper", WallVal) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("theme", ThemeTechName(gThemeId)) != DB_OK) {
        DbOk = 0;
    }
    if (DbSet("deskgrad", GradVal) != DB_OK) {
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
    } else {
        HalConsoleWriteSerial("Theme: saved THEME.CFG + TOYOS.DB\n");
    }
    sBusy = 0;
    return 0;
}
