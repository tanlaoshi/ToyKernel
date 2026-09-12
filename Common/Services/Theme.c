/*
 * Theme.c — 主题存储（PR-D2）+ THEME.CFG（PR-D6）+ mode=WxH（PR-D7）+ TOYOS.DB（PR-DB1）
 *
 * 运行时颜色/字体优先读 TOYOS.DB；写盘时先 THEME.CFG 再 DB（QEMU edid 认 CFG）。
 * 分辨率 mode=：读时若 CFG 有则覆盖 DB（与 edid/Boot 权威一致，PR-D-res）。
 * 键：desktop / shell / font / mode / scale（与 THEME.CFG 同名）。
 */
#include "Theme.h"
#include "UI.h"
#include "Font.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Db.h"
#include "Hal.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "BootInfo.h"

static UINT32 gDesktopBg = COLOR_DARK_GRAY;
static UINT32 gShellClientBg = COLOR_LIGHT_GRAY;
static UINT32 gFontId;
static UINT32 gModeW;
static UINT32 gModeH;
static UINT32 gUiScale = 100; /* 50 / 100 / 150 / 200 */

static UINT32 NormalizeUiScale(UINT32 Percent) {
    if (Percent <= 75) {
        return 50;
    }
    if (Percent <= 125) {
        return 100;
    }
    if (Percent <= 175) {
        return 150;
    }
    return 200;
}

void ThemeInit(void) {
    gDesktopBg = COLOR_DARK_GRAY;
    gShellClientBg = COLOR_LIGHT_GRAY;
    /* 默认小字：16×32 会撑爆 Store 等窄按钮；有 Sun 8x16 则用它 */
    gFontId = 2; /* Terminus 10x18；ThemeLoad 后再选 Sun */
    gModeW = 0;
    gModeH = 0;
    gUiScale = 100;
    (void)FontSetById(gFontId);
}

UINT32 ThemeDesktopBackground(void) {
    return gDesktopBg;
}

UINT32 ThemeShellClientBackground(void) {
    return gShellClientBg;
}

/* Settings 客户区底色（M10）；暂与默认浅灰一致，不单独持久化 */
UINT32 ThemeSettingsClientBackground(void) {
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeFontId(void) {
    return gFontId;
}

/* 优先 Sun 8x16，其次 Terminus 10x18；避免默认 16×32 */
static UINT32 ThemeCompactFontId(void) {
    UINT32 i;
    const FONT_FACE *F;
    UINT32 Fallback = 2;

    if (FontCount() == 0) {
        return 0;
    }
    if (Fallback >= FontCount()) {
        Fallback = FontCount() - 1;
    }
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        if (!F || !F->Name) {
            continue;
        }
        if (F->Width <= 8 && F->Height <= 16) {
            return i; /* Sun 8x16 等 */
        }
    }
    for (i = 0; i < FontCount(); i++) {
        F = FontGetById(i);
        if (!F || !F->Name) {
            continue;
        }
        if (F->Width <= 10 && F->Height <= 18) {
            return i;
        }
    }
    return Fallback;
}

UINT32 ThemeDisplayWidth(void) {
    return gModeW;
}

UINT32 ThemeDisplayHeight(void) {
    return gModeH;
}

int ThemeHasDisplayPref(void) {
    return gModeW >= 640 && gModeH >= 480;
}

void ThemeSetDisplayMode(UINT32 Width, UINT32 Height) {
    gModeW = Width;
    gModeH = Height;
}

void ThemeClearDisplayMode(void) {
    gModeW = 0;
    gModeH = 0;
}

UINT32 ThemeUiScale(void) {
    return NormalizeUiScale(gUiScale);
}

void ThemeSetUiScale(UINT32 Percent) {
    gUiScale = NormalizeUiScale(Percent);
}

int ThemeApplyUiScaleLive(UINT32 Percent) {
    UINT32 Prev = ThemeUiScale();
    UINT32 Next = NormalizeUiScale(Percent);

    gUiScale = Next;
    if (HalVideoSetUiScale(Next) != 0) {
        gUiScale = Prev;
        (void)HalVideoSetUiScale(Prev);
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}

int ThemeApplyDisplayLive(UINT32 Width, UINT32 Height) {
    const BOOT_INFO *Info;
    UINT64 Base;
    UINT64 MapBytes;
    UINT64 Need;

    if (Width < 640 || Height < 480) {
        return -1;
    }
    if (!HalVideoCanHotSetMode()) {
        return -1;
    }

    Info = BootInfoGet();
    Base = HalVideoFrameBufferBase();
    if (Base == 0 && Info) {
        Base = Info->FrameBufferBase;
    }
    if (Base == 0) {
        return -1;
    }

    Need = (UINT64)Width * (UINT64)Height * sizeof(UINT32);
    /* 映射到至少 16MiB，覆盖 Settings 最大档 1600x900 */
    MapBytes = 16ull * 1024 * 1024;
    if (Info && Info->FrameBufferSize > MapBytes) {
        MapBytes = Info->FrameBufferSize;
    }
    if (Need > MapBytes) {
        MapBytes = Need;
    }
    if (VirtualMemoryMapRange(Base, Base, (UINTN)MapBytes,
                              HalVideoFbMapFlags()) != 0) {
        return -1;
    }

    if (HalVideoSetMode(Width, Height) != 0) {
        return -1;
    }
    GuiOnDisplayResize();
    return 0;
}

void ThemeSetDesktopBackground(UINT32 Color) {
    gDesktopBg = Color;
}

void ThemeSetShellClientBackground(UINT32 Color) {
    gShellClientBg = Color;
}

int ThemeSetFontId(UINT32 Id) {
    UINT32 Prev = gFontId;

    if (FontSetById(Id) != 0) {
        (void)FontSetById(Prev);
        return -1;
    }
    gFontId = Id;
    return 0;
}

void ThemeApply(void) {
    if (gFontId >= FontCount()) {
        gFontId = FontCurrentId() < FontCount() ? FontCurrentId() : 0;
    }
    if (FontSetById(gFontId) != 0) {
        gFontId = 0;
        (void)FontSetById(0);
    }
    GuiApplyThemeColors();
    /* PR-G8：属性已更新 → 一次自下而上合成 → 备份；勿 GuiRedraw+Raise 多遍 */
    GuiComposeThemeScene();
    (void)ThemeSave();
}

/* FontReloadAssets 后：偏好 id 越界则钳到当前合法字体 */
void ThemeClampFontId(void) {
    if (gFontId >= FontCount()) {
        gFontId = FontCurrentId() < FontCount() ? FontCurrentId() : 0;
        (void)FontSetById(gFontId);
    }
}

/* ---- PR-D6 / D7 持久化 ---- */

static int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static int HexVal(char C) {
    if (C >= '0' && C <= '9') {
        return C - '0';
    }
    if (C >= 'a' && C <= 'f') {
        return C - 'a' + 10;
    }
    if (C >= 'A' && C <= 'F') {
        return C - 'A' + 10;
    }
    return -1;
}

static int ParseHexU32(const char *S, UINT32 *Out) {
    UINT32 V = 0;
    int N = 0;
    int D;

    if (!S || !Out) {
        return -1;
    }
    while (*S && IsSpace(*S)) {
        S++;
    }
    if (S[0] == '0' && (S[1] == 'x' || S[1] == 'X')) {
        S += 2;
    }
    while (*S) {
        D = HexVal(*S);
        if (D < 0) {
            break;
        }
        V = (V << 4) | (UINT32)D;
        N++;
        S++;
        if (N > 8) {
            return -1;
        }
    }
    if (N == 0) {
        return -1;
    }
    *Out = V;
    return 0;
}

static int ParseDecU32(const char *S, UINT32 *Out, const char **End) {
    UINT32 V = 0;
    int N = 0;

    if (!S || !Out) {
        return -1;
    }
    while (*S && IsSpace(*S)) {
        S++;
    }
    while (*S >= '0' && *S <= '9') {
        V = V * 10u + (UINT32)(*S - '0');
        N++;
        S++;
        if (N > 5) {
            return -1;
        }
    }
    if (N == 0) {
        return -1;
    }
    *Out = V;
    if (End) {
        *End = S;
    }
    return 0;
}

/* mode=1024x768 或 mode=auto / 0x0 */
static int ParseModeValue(const char *S, UINT32 *W, UINT32 *H) {
    const char *Rest;
    UINT32 Aw;
    UINT32 Ah;

    if (!S || !W || !H) {
        return -1;
    }
    while (*S && IsSpace(*S)) {
        S++;
    }
    if (S[0] == 'a' || S[0] == 'A') {
        /* auto */
        *W = 0;
        *H = 0;
        return 0;
    }
    if (ParseDecU32(S, &Aw, &Rest) != 0) {
        return -1;
    }
    if (*Rest != 'x' && *Rest != 'X') {
        return -1;
    }
    Rest++;
    if (ParseDecU32(Rest, &Ah, 0) != 0) {
        return -1;
    }
    if (Aw == 0 && Ah == 0) {
        *W = 0;
        *H = 0;
        return 0;
    }
    if (Aw < 640 || Ah < 480) {
        return -1;
    }
    *W = Aw;
    *H = Ah;
    return 0;
}

static const char *ValueAfterKey(const char *Line, const char *Key) {
    while (*Key) {
        if (*Line != *Key) {
            return 0;
        }
        Line++;
        Key++;
    }
    if (*Line != '=') {
        return 0;
    }
    return Line + 1;
}

static void ApplyLine(const char *Line) {
    UINT32 V;
    UINT32 W;
    UINT32 H;
    const char *Val;

    while (*Line && IsSpace(*Line)) {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return;
    }
    Val = ValueAfterKey(Line, "desktop");
    if (Val) {
        if (ParseHexU32(Val, &V) == 0) {
            gDesktopBg = V & 0x00FFFFFFu;
        }
        return;
    }
    Val = ValueAfterKey(Line, "shell");
    if (Val) {
        if (ParseHexU32(Val, &V) == 0) {
            gShellClientBg = V & 0x00FFFFFFu;
        }
        return;
    }
    Val = ValueAfterKey(Line, "font");
    if (Val) {
        if (ParseHexU32(Val, &V) == 0) {
            (void)ThemeSetFontId(V);
        }
        return;
    }
    Val = ValueAfterKey(Line, "mode");
    if (Val) {
        if (ParseModeValue(Val, &W, &H) == 0) {
            gModeW = W;
            gModeH = H;
        }
        return;
    }
    Val = ValueAfterKey(Line, "scale");
    if (Val) {
        if (ParseDecU32(Val, &V, 0) == 0) {
            gUiScale = NormalizeUiScale(V);
        }
    }
}

static void PutHex6(char *Dst, UINT32 Color) {
    static const char Hex[] = "0123456789abcdef";
    UINT32 C = Color & 0x00FFFFFFu;
    int i;

    for (i = 5; i >= 0; i--) {
        Dst[i] = Hex[C & 0xF];
        C >>= 4;
    }
}

static void PutDec(char *Dst, UINT32 V, UINTN *Len) {
    char Tmp[8];
    int N = 0;
    int i;

    if (V == 0) {
        Dst[(*Len)++] = '0';
        return;
    }
    while (V > 0 && N < (int)sizeof(Tmp)) {
        Tmp[N++] = (char)('0' + (V % 10));
        V /= 10;
    }
    for (i = N - 1; i >= 0; i--) {
        Dst[(*Len)++] = Tmp[i];
    }
}

static int ApplyDbKey(const char *Key) {
    char Val[DB_VAL_MAX];
    char Line[DB_KEY_MAX + DB_VAL_MAX + 2];
    int i = 0;
    int j;

    if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
        return 0;
    }
    while (Key[i] && i < DB_KEY_MAX - 1) {
        Line[i] = Key[i];
        i++;
    }
    Line[i++] = '=';
    for (j = 0; Val[j] && i < (int)sizeof(Line) - 1; j++) {
        Line[i++] = Val[j];
    }
    Line[i] = 0;
    ApplyLine(Line);
    return 1;
}

static int ThemeLoadFromCfg(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;

    if (FileSystemReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
        return -1;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return 0;
}

/* PR-D-res：仅覆盖 mode=（CFG 与 QEMU edid / Boot 同源） */
static int ThemeOverlayModeFromCfg(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;
    int Got = 0;
    const char *Val;

    if (FileSystemReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
        return -1;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                const char *P;

                Line[L] = 0;
                P = Line;
                while (*P && IsSpace(*P)) {
                    P++;
                }
                Val = ValueAfterKey(P, "mode");
                if (Val) {
                    ApplyLine(P);
                    Got = 1;
                }
                L = 0;
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return Got ? 0 : -1;
}

int ThemeLoad(void) {
    int FromDb;
    int ModeFromCfg;

    FromDb = ApplyDbKey("desktop") + ApplyDbKey("shell") +
             ApplyDbKey("font") + ApplyDbKey("mode") + ApplyDbKey("scale");
    if (FromDb == 0) {
        if (ThemeLoadFromCfg() != 0) {
            return -1;
        }
        HalConsoleWriteSerial("theme: loaded THEME.CFG\n");
    } else {
        ModeFromCfg = (ThemeOverlayModeFromCfg() == 0);
        if (ModeFromCfg) {
            HalConsoleWriteSerial("theme: loaded TOYOS.DB (mode from THEME.CFG)\n");
        } else {
            HalConsoleWriteSerial("theme: loaded TOYOS.DB\n");
        }
    }
    (void)FontSetById(gFontId);
    if (gFontId >= FontCount() || FontCurrentId() != gFontId) {
        ThemeClampFontId();
    }
    /* font=0 旧默认是 Terminus 16×32，自动改到紧凑字面 */
    if (gFontId == 0) {
        gFontId = ThemeCompactFontId();
        (void)FontSetById(gFontId);
    }
    gUiScale = NormalizeUiScale(gUiScale);
    DebugWrite("theme: desktop=");
    DebugHex32(gDesktopBg);
    DebugWrite(" shell=");
    DebugHex32(gShellClientBg);
    DebugWrite(" font=");
    DebugHex32(gFontId);
    DebugWrite(" scale=");
    DebugHex32(gUiScale);
    if (ThemeHasDisplayPref()) {
        DebugWrite(" mode=");
        DebugHex32(gModeW);
        DebugWrite("x");
        DebugHex32(gModeH);
    }
    DebugWrite("\n");
    return 0;
}

int ThemeSave(void) {
    char Buf[192];
    UINTN N = 0;
    char Hex[7];
    char FontVal[8];
    char ModeVal[24];
    char ScaleVal[8];
    UINTN ModeLen = 0;
    UINTN ScaleLen = 0;
    int i;
    int DbOk = 1;
    static char sLastCfg[192];
    static UINTN sLastCfgN;
    static int sBusy;

    if (sBusy) {
        HalConsoleWriteSerial("theme: save reenter skipped\n");
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
            HalConsoleWriteSerial("theme: already saved (skip)\n");
            sBusy = 0;
            return 0;
        }
    }

    if (FileSystemWriteFile(THEME_CFG_PATH, Buf, N) != FAT_OK) {
        HalConsoleWriteSerial("theme: save THEME.CFG failed\n");
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
    if (DbEndBatch() != DB_OK) {
        DbOk = 0;
    }

    if (!DbOk) {
        HalConsoleWriteSerial("theme: saved THEME.CFG (DB write failed)\n");
    } else {
        HalConsoleWriteSerial("theme: saved THEME.CFG + TOYOS.DB\n");
    }
    sBusy = 0;
    return 0;
}
