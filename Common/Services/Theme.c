/*
 * Theme.c — 主题存储（PR-D2）+ THEME.CFG 持久化（PR-D6）
 *
 * THEME.CFG 示例：
 *   desktop=404040
 *   shell=c0c0c0
 *   font=0
 */
#include "Theme.h"
#include "UI.h"
#include "Font.h"
#include "Gui.h"
#include "Console.h"
#include "SettingsUi.h"
#include "Fat.h"
#include "Hal.h"
#include "Debug.h"

static UINT32 gDesktopBg = COLOR_DARK_GRAY;
static UINT32 gShellClientBg = COLOR_LIGHT_GRAY;
static UINT32 gFontId;

void ThemeInit(void) {
    gDesktopBg = COLOR_DARK_GRAY;
    gShellClientBg = COLOR_LIGHT_GRAY;
    gFontId = 0;
    (void)FontSetById(gFontId);
}

UINT32 ThemeDesktopBg(void) {
    return gDesktopBg;
}

UINT32 ThemeShellClientBg(void) {
    return gShellClientBg;
}

UINT32 ThemeFontId(void) {
    return gFontId;
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
    /*
     * GuiRedraw 后客户区是空的。先重画 Shell 文字（可能穿透上层窗），
     * 再整窗重画 Settings（含标题栏）盖回去，最后刷新备份供拖动合成。
     */
    ConsoleRepaintShellWindows();
    SettingsUiRefresh();
    HalVideoClearClip();
    GuiBackupAllWindows();
    (void)ThemeSave();
}

/* ---- PR-D6 持久化 ---- */

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

int ThemeLoad(void) {
    static char Buf[256];
    UINTN Size = 0;
    UINTN i;
    char Line[64];
    UINTN L;

    if (!FatReadFile(THEME_CFG_PATH, Buf, sizeof(Buf) - 1, &Size) || Size == 0) {
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
    DebugWrite("\n");
    return 0;
}

int ThemeSave(void) {
    char Buf[128];
    UINTN N = 0;
    char Hex[7];

    /* desktop=XXXXXX\nshell=XXXXXX\nfont=N\n */
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
    {
        int i;
        for (i = 0; Hex[i]; i++) {
            Buf[N++] = Hex[i];
        }
    }
    Buf[N++] = '\n';

    Buf[N++] = 's';
    Buf[N++] = 'h';
    Buf[N++] = 'e';
    Buf[N++] = 'l';
    Buf[N++] = 'l';
    Buf[N++] = '=';
    PutHex6(Hex, gShellClientBg);
    {
        int i;
        for (i = 0; Hex[i]; i++) {
            Buf[N++] = Hex[i];
        }
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
    Buf[N] = 0;

    if (!FatWriteFile(THEME_CFG_PATH, Buf, N)) {
        HalConsoleWriteSerial("theme: save THEME.CFG failed\n");
        return -1;
    }
    HalConsoleWriteSerial("theme: saved THEME.CFG\n");
    return 0;
}
