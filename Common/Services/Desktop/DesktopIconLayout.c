/*
 * DesktopIconLayout.c — 图标坐标读写与摆放
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

static int ParseIconXy(const char *S, UINT32 *OutX, UINT32 *OutY) {
    UINT32 X = 0;
    UINT32 Y = 0;
    const char *P;

    if (!S || !OutX || !OutY) {
        return 0;
    }
    P = S;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        X = X * 10u + (UINT32)(*P - '0');
        P++;
        if (X > 8192u) {
            return 0;
        }
    }
    if (*P != ',') {
        return 0;
    }
    P++;
    if (*P < '0' || *P > '9') {
        return 0;
    }
    while (*P >= '0' && *P <= '9') {
        Y = Y * 10u + (UINT32)(*P - '0');
        P++;
        if (Y > 8192u) {
            return 0;
        }
    }
    if (*P != '\0') {
        return 0;
    }
    *OutX = X;
    *OutY = Y;
    return 1;
}

static void FormatIconXy(UINT32 X, UINT32 Y, char *Out, UINTN OutMax) {
    char Tmp[16];
    UINTN N = 0;
    UINTN i;
    UINT32 V;

    if (!Out || OutMax < 4) {
        return;
    }
    V = X;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    i = 0;
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    if (i + 1 < OutMax) {
        Out[i++] = ',';
    }
    N = 0;
    V = Y;
    if (V == 0) {
        Tmp[N++] = '0';
    } else {
        while (V > 0 && N < sizeof(Tmp)) {
            Tmp[N++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (N > 0 && i + 1 < OutMax) {
        Out[i++] = Tmp[--N];
    }
    Out[i] = '\0';
}

static const char *IconLayoutKey(int Idx) {
    switch (Idx) {
    case 0:
        return "ic0";
    case 1:
        return "ic1";
    case 2:
        return "ic2";
    case 3:
        return "ic3";
    case 4:
        return "ic4";
    case 5:
        return "ic5";
    default:
        return 0;
    }
}

/* PR-G-desk-1：从 TOYOS.DB 覆盖默认坐标 */
void LoadIconLayout(void) {
    int i;
    int Any = 0;
    char Val[DB_VAL_MAX];
    UINT32 X;
    UINT32 Y;
    const char *Key;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        if (DbGet(Key, Val, sizeof(Val)) != DB_OK) {
            continue;
        }
        if (!ParseIconXy(Val, &X, &Y)) {
            continue;
        }
        SnapIconToGrid(&X, &Y);
        gIcons[i].X = X;
        gIcons[i].Y = Y;
        Any = 1;
    }
    if (Any) {
        DebugWrite("desktop: icon layout from TOYOS.DB\n");
    }
}

void SaveIconLayout(void) {
    int i;
    char Val[DB_VAL_MAX];
    const char *Key;
    int Ok = 1;

    DbBeginBatch();
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        Key = IconLayoutKey(i);
        if (!Key) {
            continue;
        }
        FormatIconXy(gIcons[i].X, gIcons[i].Y, Val, sizeof(Val));
        if (DbSet(Key, Val) != DB_OK) {
            Ok = 0;
        }
    }
    if (DbEndBatch() != DB_OK) {
        Ok = 0;
    }
    if (Ok) {
        DebugWrite("desktop: icon layout saved\n");
    } else {
        DebugWrite("desktop: icon layout save failed\n");
    }
}

void PlaceDesktopIcons(void) {
    UINT32 RowH = DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
                  DESKTOP_ICON_GAP;

    gIcons[0].Action = DESKTOP_ACTION_SHELL;
    gIcons[0].ExecPath = 0;
    gIcons[0].IconColor = COLOR_BLUE;
    gIcons[0].BmpPath = "Assets/Icons/bmp48/SHELL.BMP";
    gIcons[0].X = DESKTOP_ORIGIN_X;
    gIcons[0].Y = DESKTOP_ORIGIN_Y;

    gIcons[1].Action = DESKTOP_ACTION_SETTINGS;
    gIcons[1].IconColor = 0x00606080;
    gIcons[1].BmpPath = "Assets/Icons/bmp48/SET.BMP";
    gIcons[1].X = DESKTOP_ORIGIN_X;
    gIcons[1].Y = DESKTOP_ORIGIN_Y + RowH;

    gIcons[2].Action = DESKTOP_ACTION_FILES;
    gIcons[2].IconColor = 0x00208040;
    gIcons[2].BmpPath = "Assets/Icons/bmp48/FILES.BMP";
    gIcons[2].X = DESKTOP_ORIGIN_X;
    gIcons[2].Y = DESKTOP_ORIGIN_Y + RowH * 2;

    gIcons[3].Action = DESKTOP_ACTION_STORE;
    gIcons[3].IconColor = 0x002080C0;
    gIcons[3].BmpPath = "Assets/Icons/bmp48/STORE.BMP";
    gIcons[3].X = DESKTOP_ORIGIN_X;
    gIcons[3].Y = DESKTOP_ORIGIN_Y + RowH * 3;

    gIcons[4].Action = DESKTOP_ACTION_DEVICES;
    gIcons[4].ExecPath = 0;
    gIcons[4].IconColor = 0x00806040;
    gIcons[4].BmpPath = "Assets/Icons/bmp48/INFO.BMP";
    gIcons[4].X = DESKTOP_ORIGIN_X;
    gIcons[4].Y = DESKTOP_ORIGIN_Y + RowH * 4;

    gIcons[5].Action = DESKTOP_ACTION_EXEC;
    gIcons[5].ExecPath = "SNAKE.ELF";
    gIcons[5].IconColor = 0x0040C080;
    gIcons[5].BmpPath = "Assets/Icons/bmp48/GAME.BMP";
    gIcons[5].X = DESKTOP_ORIGIN_X;
    gIcons[5].Y = DESKTOP_ORIGIN_Y + RowH * 5;

    DesktopRefreshLabels();
}

void MoveIconTo(int Idx, UINT32 NewX, UINT32 NewY) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    ClampIconPos(&NewX, &NewY);
    if (gIcons[Idx].X == NewX && gIcons[Idx].Y == NewY) {
        return;
    }
    IconBounds(&gIcons[Idx], &Ox, &Oy, &Ow, &Oh);
    gIcons[Idx].X = NewX;
    gIcons[Idx].Y = NewY;
    /* 拖动中图标置顶：整块擦脚印并还原窗/影，再 Raw 画在最前 */
    ClearIconFootprint(Ox, Oy, Ow, Oh);
    DrawOneIconRaw(&gIcons[Idx], Idx == gDeskSelected);
    HalVideoPresent();
}
