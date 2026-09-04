/*
 * Theme.c — 主题存储（PR-D2）+ THEME.CFG（PR-D6）+ mode=WxH（PR-D7）
 *
 * THEME.CFG 示例：
 *   desktop=404040
 *   shell=c0c0c0
 *   font=0
 *   mode=1024x768
 */
#include "Theme.h"
#include "UI.h"
#include "Font.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Hal.h"
#include "Debug.h"

static UINT32 gDesktopBg = COLOR_DARK_GRAY;
static UINT32 gShellClientBg = COLOR_LIGHT_GRAY;
static UINT32 gFontId;
static UINT32 gModeW;
static UINT32 gModeH;

void ThemeInit(void) {
    gDesktopBg = COLOR_DARK_GRAY;
    gShellClientBg = COLOR_LIGHT_GRAY;
    gFontId = 0;
    gModeW = 0;
    gModeH = 0;
    (void)FontSetById(gFontId);
}

UINT32 ThemeDesktopBg(void) {
    return gDesktopBg;
}

UINT32 ThemeShellClientBg(void) {
    return gShellClientBg;
}

/* Settings 客户区底色（M10）；暂与默认浅灰一致，不单独持久化 */
UINT32 ThemeSettingsClientBg(void) {
    return COLOR_LIGHT_GRAY;
}

UINT32 ThemeFontId(void) {
    return gFontId;
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

void ThemeSetDesktopBg(UINT32 Color) {
    gDesktopBg = Color;
}

void ThemeSetShellClientBg(UINT32 Color) {
    gShellClientBg = Color;
}

int ThemeSetFontId(UINT32 Id) {
    if (FontSetById(Id) != 0) {
        return -1;
    }
    gFontId = Id;
    return 0;
}

void ThemeApply(void) {
    (void)FontSetById(gFontId);
    GuiApplyThemeColors();
    /* PR-G8：属性已更新 → 一次自下而上合成 → 备份；勿 GuiRedraw+Raise 多遍 */
    GuiComposeThemeScene();
    (void)ThemeSave();
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

int ThemeLoad(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;

    if (FsReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) != FAT_OK || Size == 0) {
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
    (void)FontSetById(gFontId);
    HalConsoleWriteSerial("theme: loaded THEME.CFG\n");
    DebugWrite("theme: desktop=");
    DebugHex32(gDesktopBg);
    DebugWrite(" shell=");
    DebugHex32(gShellClientBg);
    DebugWrite(" font=");
    DebugHex32(gFontId);
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
    char Buf[160];
    UINTN N = 0;
    char Hex[7];
    int i;

    /* desktop=XXXXXX\nshell=XXXXXX\nfont=N\n[mode=WxH\n] */
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
    if (gFontId >= 10) {
        Buf[N++] = (char)('0' + (gFontId / 10) % 10);
    }
    Buf[N++] = (char)('0' + (gFontId % 10));
    Buf[N++] = '\n';

    if (ThemeHasDisplayPref()) {
        Buf[N++] = 'm';
        Buf[N++] = 'o';
        Buf[N++] = 'd';
        Buf[N++] = 'e';
        Buf[N++] = '=';
        PutDec(Buf, gModeW, &N);
        Buf[N++] = 'x';
        PutDec(Buf, gModeH, &N);
        Buf[N++] = '\n';
    }
    Buf[N] = 0;

    if (FsWriteFile(THEME_CFG_PATH, Buf, N) != FAT_OK) {
        HalConsoleWriteSerial("theme: save THEME.CFG failed\n");
        return -1;
    }
    HalConsoleWriteSerial("theme: saved THEME.CFG\n");
    return 0;
}
