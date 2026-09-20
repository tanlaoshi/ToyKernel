/*
 * ThemeParse.c — THEME.CFG / DB 行解析
 * 核心：Theme.c
 */
#include "Theme.h"
#include "ThemePrivate.h"

int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

int HexVal(char C) {
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

int ParseHexU32(const char *S, UINT32 *Out) {
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

int ParseDecU32(const char *S, UINT32 *Out, const char **End) {
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
int ParseModeValue(const char *S, UINT32 *W, UINT32 *H) {
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

const char *ValueAfterKey(const char *Line, const char *Key) {
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

void ApplyLine(const char *Line) {
    UINT32 V;
    UINT32 W;
    UINT32 H;
    int I;
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
            gThemeUiScale = NormalizeUiScale(V);
        }
        return;
    }
    Val = ValueAfterKey(Line, "fade");
    if (Val) {
        if (ParseDecU32(Val, &V, 0) == 0) {
            ThemeSetWindowFadeSteps(V);
        }
        return;
    }
    Val = ValueAfterKey(Line, "wallpaper");
    if (Val) {
        if (ParseDecU32(Val, &V, 0) == 0) {
            gWallpaper = (V != 0) ? 1 : 0;
        }
        return;
    }
    Val = ValueAfterKey(Line, "theme");
    if (Val) {
        if (ThemeTechParseName(Val, &I) == 0) {
            gThemeId = I;
        }
        return;
    }
    Val = ValueAfterKey(Line, "deskgrad");
    if (Val) {
        if (ParseDecU32(Val, &V, 0) == 0) {
            gDesktopGrad = (V != 0) ? 1 : 0;
        }
        return;
    }
}

int ApplyDbKey(const char *Key) {
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
