/*
 * SettingsUi.c — Settings 三分栏（类 Files：左大类 / 中条目 / 右详情）
 *
 * 分辨率：ThemeSave → TOYOS.DB + THEME.CFG；QEMU 优先 ThemeApplyDisplayLive。
 * 点选中间条目即应用（与旧子页语义一致）；数字键选条目。
 */
#include "SettingsUi.h"
#include "Gui.h"
#include "Theme.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Debug.h"
#include "Locale.h"

typedef enum {
    SETTINGS_CAT_DESKTOP = 0,
    SETTINGS_CAT_SHELL,
    SETTINGS_CAT_FONT,
    SETTINGS_CAT_DISPLAY,
    SETTINGS_CAT_LANGUAGE,
    SETTINGS_CAT_SCALE,
    SETTINGS_CAT_COUNT
} SETTINGS_CAT;

typedef struct {
    const char *Label;
    UINT32      Color;
} SETTINGS_COLOR;

typedef struct {
    const char *Label;
    UINT32      W;
    UINT32      H;
} SETTINGS_MODE;

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    int    Kind;   /* 0=左栏 1=中栏 2=右钮 */
    int    Index;
} SETTINGS_HIT;

#define SETTINGS_HIT_MAX  48
#define SETTINGS_SIDE_W   128u
#define SETTINGS_SIDE_BG  0x00A0A8B0u
#define SETTINGS_PREV_BG  0x00D8D8E0u
#define SETTINGS_SB_W     12u

static SETTINGS_CAT gCat = SETTINGS_CAT_DESKTOP;
static int gItemSel;
static int gItemScroll;
static int gDisplayHint; /* 0=无；1=须重启；2=已热切 */
static SETTINGS_HIT gHits[SETTINGS_HIT_MAX];
static int gHitCount;
static int gHoverKind = -1;
static int gHoverIdx = -1;
static int gPressKind = -1;
static int gPressIdx = -1;

/* 布局（Paint 写入，Click/Pointer 读取） */
static UINT32 gSideX, gSideY, gSideW, gSideRow0, gSideLineH;
static UINT32 gListX, gListTop, gListRowW, gListLineH;
static int gListVisible;
static UINT32 gSbX, gSbY, gSbW, gSbH;
static int gSbVisible;
static UINT32 gPrevX, gPrevW;

static const SETTINGS_COLOR gDesktopColors[] = {
    { "Dark Gray", COLOR_DARK_GRAY },
    { "Blue",      COLOR_BLUE },
    { "Green",     COLOR_GREEN },
    { "Black",     COLOR_BLACK },
    { "Gray",      COLOR_GRAY },
};

static const SETTINGS_COLOR gShellColors[] = {
    { "Light Gray", COLOR_LIGHT_GRAY },
    { "White",      COLOR_WHITE },
    { "Cyan",       COLOR_CYAN },
    { "Yellow",     COLOR_YELLOW },
    { "Gray",       COLOR_GRAY },
};

static const SETTINGS_MODE gModesFallback[] = {
    { "800x600",    800,  600 },
    { "1024x768",  1024,  768 },
    { "1280x720",  1280,  720 },
    { "1600x900",  1600,  900 },
    { "1920x1080", 1920, 1080 },
};

static SETTINGS_MODE gModes[BOOT_VIDEO_MODE_MAX];
static char gModeLabels[BOOT_VIDEO_MODE_MAX][16];
static int gModeCount;
static int gModesReady;
static const UINT32 gScales[] = { 50, 100, 150, 200 };

#define DESKTOP_COLOR_COUNT \
    ((int)(sizeof(gDesktopColors) / sizeof(gDesktopColors[0])))
#define SHELL_COLOR_COUNT \
    ((int)(sizeof(gShellColors) / sizeof(gShellColors[0])))
#define SCALE_COUNT ((int)(sizeof(gScales) / sizeof(gScales[0])))

static void FormatUxU(char *Out, UINTN Max, UINT32 A, UINT32 B);
static void PaintMenu(void);

static const char *CatLabel(SETTINGS_CAT C) {
    /* 三分栏左侧短名（locale 已去掉「N.」与括号说明） */
    switch (C) {
    case SETTINGS_CAT_DESKTOP:  return LocStr(MSG_SET_DESKTOP_BG);
    case SETTINGS_CAT_SHELL:    return LocStr(MSG_SET_SHELL_BG);
    case SETTINGS_CAT_FONT:     return LocStr(MSG_SET_FONT);
    case SETTINGS_CAT_DISPLAY:  return LocStr(MSG_SET_DISPLAY);
    case SETTINGS_CAT_LANGUAGE: return LocStr(MSG_SET_LANGUAGE);
    case SETTINGS_CAT_SCALE:    return LocStr(MSG_SET_SCALE);
    default:                    return "?";
    }
}

static void EnsureDisplayModes(void) {
    UINT32 N;
    UINT32 i;
    UINT32 W;
    UINT32 H;
    UINTN L;

    if (gModesReady) {
        return;
    }
    gModesReady = 1;
    gModeCount = 0;
    N = HalVideoModeCount();
    if (N > BOOT_VIDEO_MODE_MAX) {
        N = BOOT_VIDEO_MODE_MAX;
    }
    for (i = 0; i < N; i++) {
        if (HalVideoModeGet(i, &W, &H) != 0 || W < 640 || H < 480) {
            continue;
        }
        FormatUxU(gModeLabels[gModeCount], sizeof(gModeLabels[0]), W, H);
        L = 0;
        while (gModeLabels[gModeCount][L]) {
            L++;
        }
        gModes[gModeCount].Label = gModeLabels[gModeCount];
        gModes[gModeCount].W = W;
        gModes[gModeCount].H = H;
        gModeCount++;
    }
    if (gModeCount == 0) {
        N = (UINT32)(sizeof(gModesFallback) / sizeof(gModesFallback[0]));
        for (i = 0; i < N && gModeCount < BOOT_VIDEO_MODE_MAX; i++) {
            gModes[gModeCount] = gModesFallback[i];
            gModeCount++;
        }
    }
}

static int ModeCount(void) {
    EnsureDisplayModes();
    return gModeCount;
}

static int ItemCount(void) {
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:  return DESKTOP_COLOR_COUNT;
    case SETTINGS_CAT_SHELL:    return SHELL_COLOR_COUNT;
    case SETTINGS_CAT_FONT:     return (int)FontCount();
    case SETTINGS_CAT_DISPLAY:  return 1 + ModeCount();
    case SETTINGS_CAT_LANGUAGE: return 2;
    case SETTINGS_CAT_SCALE:    return SCALE_COUNT;
    default:                    return 0;
    }
}

static void CopyStr(char *Dst, int DstMax, const char *S) {
    int j;

    if (DstMax <= 0) {
        return;
    }
    if (!S) {
        Dst[0] = 0;
        return;
    }
    for (j = 0; S[j] && j < DstMax - 1; j++) {
        Dst[j] = S[j];
    }
    Dst[j] = 0;
}

static void ItemLabel(int Idx, char *Out, int OutMax) {
    const FONT_FACE *Face;
    UINT32 Sc;
    int P;

    if (OutMax <= 0) {
        return;
    }
    Out[0] = 0;
    if (Idx < 0 || Idx >= ItemCount()) {
        return;
    }
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        CopyStr(Out, OutMax, gDesktopColors[Idx].Label);
        break;
    case SETTINGS_CAT_SHELL:
        CopyStr(Out, OutMax, gShellColors[Idx].Label);
        break;
    case SETTINGS_CAT_FONT:
        Face = FontGetById((UINT32)Idx);
        CopyStr(Out, OutMax, (Face && Face->Name) ? Face->Name : "?");
        break;
    case SETTINGS_CAT_DISPLAY:
        if (Idx == 0) {
            CopyStr(Out, OutMax, "Auto");
        } else {
            CopyStr(Out, OutMax, gModes[Idx - 1].Label);
        }
        break;
    case SETTINGS_CAT_LANGUAGE:
        CopyStr(Out, OutMax,
                (Idx == 0) ? LocStr(MSG_SET_LANG_EN) : LocStr(MSG_SET_LANG_ZH));
        break;
    case SETTINGS_CAT_SCALE:
        Sc = gScales[Idx];
        P = 0;
        if (Sc >= 100) {
            Out[P++] = (char)('0' + (Sc / 100) % 10);
        }
        Out[P++] = (char)('0' + (Sc / 10) % 10);
        Out[P++] = (char)('0' + (Sc % 10));
        Out[P++] = '%';
        Out[P] = 0;
        break;
    default:
        break;
    }
}

/* 当前已生效值在列表中的下标 */
static int CurrentItemIndex(void) {
    int i;
    UINT32 Cur;
    UINT32 PrefW;
    UINT32 PrefH;
    int HasPref;

    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        Cur = ThemeDesktopBackground();
        for (i = 0; i < DESKTOP_COLOR_COUNT; i++) {
            if (gDesktopColors[i].Color == Cur) {
                return i;
            }
        }
        return 0;
    case SETTINGS_CAT_SHELL:
        Cur = ThemeShellClientBackground();
        for (i = 0; i < SHELL_COLOR_COUNT; i++) {
            if (gShellColors[i].Color == Cur) {
                return i;
            }
        }
        return 0;
    case SETTINGS_CAT_FONT:
        return (int)ThemeFontId();
    case SETTINGS_CAT_DISPLAY:
        HasPref = ThemeHasDisplayPref();
        if (!HasPref) {
            return 0;
        }
        PrefW = ThemeDisplayWidth();
        PrefH = ThemeDisplayHeight();
        for (i = 0; i < ModeCount(); i++) {
            if (gModes[i].W == PrefW && gModes[i].H == PrefH) {
                return i + 1;
            }
        }
        return 0;
    case SETTINGS_CAT_LANGUAGE:
        return (LocaleGet() == LOC_LANG_ZH) ? 1 : 0;
    case SETTINGS_CAT_SCALE:
        Cur = ThemeUiScale();
        for (i = 0; i < SCALE_COUNT; i++) {
            if (gScales[i] == Cur) {
                return i;
            }
        }
        return 1;
    default:
        return 0;
    }
}

static int FocusSettingsWindow(void) {
    int i;

    if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        return 1;
    }
    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) == GUI_WIN_SETTINGS) {
            GuiRaiseToFront(i);
            return 1;
        }
    }
    return 0;
}

static void FormatNowDisplay(char *Out, UINTN Max) {
    UINT32 PhysW = 0;
    UINT32 PhysH = 0;
    UINT32 LogW = 0;
    UINT32 LogH = 0;
    UINT32 Sc;
    UINTN N = 0;

    if (Max == 0) {
        return;
    }
    HalVideoGetPhysicalSize(&PhysW, &PhysH);
    HalVideoGetSize(&LogW, &LogH);
    if (PhysW == 0 || PhysH == 0) {
        PhysW = LogW;
        PhysH = LogH;
    }
    Out[0] = 'N';
    Out[1] = 'o';
    Out[2] = 'w';
    Out[3] = ' ';
    FormatUxU(Out + 4, Max > 4 ? Max - 4 : 0, PhysW, PhysH);
    while (Out[N]) {
        N++;
    }
    Sc = ThemeUiScale();
    if (N + 12 < Max) {
        Out[N++] = ' ';
        Out[N++] = 's';
        Out[N++] = 'c';
        Out[N++] = 'a';
        Out[N++] = 'l';
        Out[N++] = 'e';
        Out[N++] = '=';
        if (Sc >= 100) {
            Out[N++] = (char)('0' + (Sc / 100) % 10);
        }
        Out[N++] = (char)('0' + (Sc / 10) % 10);
        Out[N++] = (char)('0' + (Sc % 10));
        Out[N++] = '%';
        Out[N] = 0;
    }
    if (Sc != 100 && (LogW != PhysW || LogH != PhysH) && N + 16 < Max) {
        Out[N++] = ' ';
        Out[N++] = 'U';
        Out[N++] = 'I';
        Out[N++] = ' ';
        FormatUxU(Out + N, Max - N, LogW, LogH);
    }
}

static void FormatUxU(char *Out, UINTN Max, UINT32 A, UINT32 B) {
    UINTN N = 0;
    char Tmp[8];
    int Tn;
    int i;
    UINT32 V;

    if (Max == 0) {
        return;
    }
    V = A;
    Tn = 0;
    if (V == 0) {
        Tmp[Tn++] = '0';
    } else {
        while (V > 0 && Tn < (int)sizeof(Tmp)) {
            Tmp[Tn++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    for (i = Tn - 1; i >= 0 && N + 1 < Max; i--) {
        Out[N++] = Tmp[i];
    }
    if (N + 1 < Max) {
        Out[N++] = 'x';
    }
    V = B;
    Tn = 0;
    if (V == 0) {
        Tmp[Tn++] = '0';
    } else {
        while (V > 0 && Tn < (int)sizeof(Tmp)) {
            Tmp[Tn++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    for (i = Tn - 1; i >= 0 && N + 1 < Max; i--) {
        Out[N++] = Tmp[i];
    }
    Out[N] = 0;
}

static void HitClear(void) {
    gHitCount = 0;
}

static void HitAdd(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Kind, int Index) {
    if (gHitCount >= SETTINGS_HIT_MAX || W == 0 || H == 0) {
        return;
    }
    gHits[gHitCount].X = X;
    gHits[gHitCount].Y = Y;
    gHits[gHitCount].W = W;
    gHits[gHitCount].H = H;
    gHits[gHitCount].Kind = Kind;
    gHits[gHitCount].Index = Index;
    gHitCount++;
}

static void ClampItemScroll(void) {
    int N = ItemCount();

    if (gItemSel < 0) {
        gItemSel = 0;
    }
    if (N > 0 && gItemSel >= N) {
        gItemSel = N - 1;
    }
    if (gListVisible < 1) {
        gListVisible = 1;
    }
    if (gItemSel < gItemScroll) {
        gItemScroll = gItemSel;
    }
    if (gItemSel >= gItemScroll + gListVisible) {
        gItemScroll = gItemSel - gListVisible + 1;
    }
    if (gItemScroll < 0) {
        gItemScroll = 0;
    }
}

static void SelectCategory(SETTINGS_CAT C) {
    if (C < 0 || C >= SETTINGS_CAT_COUNT) {
        return;
    }
    gCat = C;
    gItemSel = CurrentItemIndex();
    gItemScroll = 0;
    ClampItemScroll();
}

static void ApplyDesktopColor(int Index) {
    if (Index < 0 || Index >= DESKTOP_COLOR_COUNT) {
        return;
    }
    ThemeSetDesktopBackground(gDesktopColors[Index].Color);
    ThemeApply();
}

static void ApplyShellColor(int Index) {
    if (Index < 0 || Index >= SHELL_COLOR_COUNT) {
        return;
    }
    ThemeSetShellClientBackground(gShellColors[Index].Color);
    ThemeApply();
}

static void ApplyFont(int Index) {
    if (Index < 0 || (UINT32)Index >= FontCount()) {
        return;
    }
    if (ThemeSetFontId((UINT32)Index) != 0) {
        return;
    }
    ThemeApply();
}

static void ApplyScaleChoice(int Index) {
    static int sBusy;

    if (Index < 0 || Index >= SCALE_COUNT) {
        return;
    }
    if (sBusy) {
        return;
    }
    sBusy = 1;
    GuiInputLock(1);
    if (ThemeApplyUiScaleLive(gScales[Index]) == 0) {
        gDisplayHint = 2;
        (void)ThemeSave();
    } else {
        gDisplayHint = 0;
    }
    GuiInputLock(0);
    sBusy = 0;
    PaintMenu();
}

static void ApplyDisplayChoice(int Index) {
    UINT32 W = 0;
    UINT32 H = 0;
    int Live = 0;
    int Saved = 0;
    static int sBusy;

    if (sBusy) {
        return;
    }
    sBusy = 1;
    GuiInputLock(1);

    if (Index == 0) {
        ThemeClearDisplayMode();
    } else if (Index >= 1 && Index <= ModeCount()) {
        W = gModes[Index - 1].W;
        H = gModes[Index - 1].H;
        ThemeSetDisplayMode(W, H);
    } else {
        GuiInputLock(0);
        sBusy = 0;
        return;
    }

    if (Index >= 1 && W != 0 && H != 0) {
        char Dim[24];
        FormatUxU(Dim, sizeof(Dim), W, H);
        HalConsoleWriteSerial("settings: apply ");
        HalConsoleWriteSerial(Dim);
        HalConsoleWriteSerial("\n");
        if (ThemeApplyDisplayLive(W, H) == 0) {
            Live = 1;
            HalConsoleWriteSerial("settings: display applied live\n");
            DebugWrite("settings: display applied live\n");
        }
    }

    if (ThemeSave() == 0) {
        Saved = 1;
    } else {
        HalConsoleWriteSerial("settings: display save failed\n");
        DebugWrite("settings: display save failed\n");
    }

    if (Live) {
        gDisplayHint = 2;
        HalConsoleWriteSerial(
            "settings: live OK; cold boot still needs quit QEMU + ./run-split.sh\n");
    } else if (Saved) {
        gDisplayHint = 1;
        if (HalCpuIsHypervisor()) {
            HalConsoleWriteSerial(
                "settings: display saved; quit QEMU window, then ./run-split.sh (edid)\n");
            DebugWrite("settings: display pref saved (relaunch QEMU on VM)\n");
        } else {
            HalConsoleWriteSerial(
                "settings: display pref saved; reboot real PC to apply THEME.CFG\n");
            DebugWrite("settings: display pref saved (real PC reboot)\n");
        }
    } else {
        gDisplayHint = 0;
    }
    GuiInputLock(0);
    sBusy = 0;
    PaintMenu();
}

static void ApplyItem(int Idx) {
    if (Idx < 0 || Idx >= ItemCount()) {
        return;
    }
    gItemSel = Idx;
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        ApplyDesktopColor(Idx);
        break;
    case SETTINGS_CAT_SHELL:
        ApplyShellColor(Idx);
        break;
    case SETTINGS_CAT_FONT:
        ApplyFont(Idx);
        break;
    case SETTINGS_CAT_DISPLAY:
        ApplyDisplayChoice(Idx);
        return;
    case SETTINGS_CAT_LANGUAGE:
        (void)LocaleSet(Idx == 0 ? LOC_LANG_EN : LOC_LANG_ZH);
        break;
    case SETTINGS_CAT_SCALE:
        ApplyScaleChoice(Idx);
        return;
    default:
        break;
    }
    PaintMenu();
}

static void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Ty;
    UINT32 MaxY;
    char Line[64];
    char Item[40];
    UINT32 Swatch;
    UINT32 PrefW;
    UINT32 PrefH;
    UINT32 NowW;
    UINT32 NowH;

    LineH = FontAdvanceY();
    if (LineH < 14) {
        LineH = 14;
    }
    HalVideoFillRect(X, Y, W, H, SETTINGS_PREV_BG);
    if (W > 3) {
        HalVideoFillRect(X, Y, 3, H, COLOR_DARK_GRAY);
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    HalVideoDrawStringAt(X + 10, Ty, "Detail", COLOR_BLACK);
    Ty += LineH + 4;
    HalVideoDrawStringAt(X + 10, Ty, CatLabel(gCat), COLOR_DARK_GRAY);
    Ty += LineH + 2;

    ItemLabel(gItemSel, Item, (int)sizeof(Item));
    if (Item[0] && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, Item, COLOR_BLACK);
        Ty += LineH + 4;
    }

    if (gCat == SETTINGS_CAT_DESKTOP || gCat == SETTINGS_CAT_SHELL) {
        Swatch = (gCat == SETTINGS_CAT_DESKTOP)
                     ? ((gItemSel >= 0 && gItemSel < DESKTOP_COLOR_COUNT)
                            ? gDesktopColors[gItemSel].Color
                            : ThemeDesktopBackground())
                     : ((gItemSel >= 0 && gItemSel < SHELL_COLOR_COUNT)
                            ? gShellColors[gItemSel].Color
                            : ThemeShellClientBackground());
        if (Ty + 36 < MaxY && W > 24) {
            UiFillRectangle(X + 10, Ty, W > 40 ? 48 : W - 20, 28, Swatch);
            UiDrawRectangle(X + 10, Ty, W > 40 ? 48 : W - 20, 28, COLOR_DARK_GRAY);
            Ty += 36;
        }
    } else if (gCat == SETTINGS_CAT_FONT && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "The quick brown fox", COLOR_BLACK);
        Ty += LineH + 4;
    } else if (gCat == SETTINGS_CAT_DISPLAY) {
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty,
                                 HalCpuIsHypervisor()
                                     ? "Change may need quit QEMU + rerun"
                                     : "Change may need reboot to apply",
                                 COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
        if (ThemeHasDisplayPref()) {
            PrefW = ThemeDisplayWidth();
            PrefH = ThemeDisplayHeight();
            HalVideoGetPhysicalSize(&NowW, &NowH);
            if (NowW == 0 || NowH == 0) {
                HalVideoGetSize(&NowW, &NowH);
            }
            if ((PrefW != NowW || PrefH != NowH) && Ty + LineH < MaxY) {
                HalVideoDrawStringAt(
                    X + 10, Ty,
                    LocStr(HalCpuIsHypervisor() ? MSG_SET_PREF_DIFF : MSG_SET_PREF_DIFF_PC),
                    COLOR_BLUE);
                Ty += LineH + 2;
            }
        }
    } else if (gCat == SETTINGS_CAT_SCALE && Ty + LineH * 2 < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "50=small 100=normal", COLOR_DARK_GRAY);
        Ty += LineH;
        HalVideoDrawStringAt(X + 10, Ty, "150/200=large", COLOR_DARK_GRAY);
        Ty += LineH + 2;
        FormatNowDisplay(Line, sizeof(Line));
        if (Ty + LineH < MaxY) {
            HalVideoDrawStringAt(X + 10, Ty, Line, COLOR_DARK_GRAY);
            Ty += LineH + 2;
        }
    }

    if (gDisplayHint == 2 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "Applied (live)", COLOR_BLUE);
        Ty += LineH;
    } else if (gDisplayHint == 1 && Ty + LineH < MaxY) {
        HalVideoDrawStringAt(
            X + 10, Ty,
            LocStr(HalCpuIsHypervisor() ? MSG_SET_SAVED : MSG_SET_SAVED_PC), COLOR_BLUE);
        Ty += LineH;
    }
    if (Ty + LineH < MaxY) {
        HalVideoDrawStringAt(X + 10, Ty, "Click item to apply", COLOR_DARK_GRAY);
    }
}

static void PaintMenu(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 SideW;
    UINT32 ContentX, ContentW;
    UINT32 ListW;
    UINT32 RowY;
    UINT32 RowW;
    int i;
    int N;
    int Applied;
    char Label[48];
    char Row[56];

    if (GuiFocusKind() != GUI_WIN_SETTINGS) {
        if (!FocusSettingsWindow()) {
            return;
        }
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }

    HitClear();
    GuiFrameBufferBegin();
    HalVideoFillRect(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    SideW = 0;
    gSideW = 0;
    if (Cw > SETTINGS_SIDE_W + 160u) {
        SideW = SETTINGS_SIDE_W;
    }
    ContentX = Cx + SideW;
    ContentW = Cw - SideW;

    if (SideW > 0) {
        gSideX = Cx;
        gSideY = Cy;
        gSideW = SideW;
        gSideLineH = LineH;
        gSideRow0 = Cy + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;
        HalVideoFillRect(Cx, Cy, SideW, Ch, SETTINGS_SIDE_BG);
        if (SideW > 3) {
            HalVideoFillRect(Cx + SideW - 3, Cy, 3, Ch, COLOR_DARK_GRAY);
        }
        HalVideoDrawStringAt(Cx + 8, Cy + 8, LocStr(MSG_SET_TITLE), COLOR_BLACK);
        for (i = 0; i < SETTINGS_CAT_COUNT; i++) {
            UiDrawListRow(Cx + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          CatLabel((SETTINGS_CAT)i),
                          i == (int)gCat,
                          gHoverKind == 0 && gHoverIdx == i);
            HitAdd(Cx + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH, 0, i);
        }
    }

    gPrevW = 0;
    gPrevX = ContentX;
    if (ContentW > 360u) {
        gPrevW = ContentW * 2u / 5u;
        if (gPrevW < 160u) {
            gPrevW = 160u;
        }
        if (gPrevW + 120u > ContentW) {
            gPrevW = ContentW > 120u ? ContentW - 120u : 0;
        }
    }

    ListW = ContentW - gPrevW;
    gListX = ContentX;
    gListTop = Cy + 8 + LineH + 4;
    gListLineH = LineH;
    N = ItemCount();
    gListVisible = 1;
    if (Ch > 8 + LineH * 2) {
        gListVisible = (int)((Ch - 8 - LineH * 2) / LineH);
    }
    if (gListVisible < 1) {
        gListVisible = 1;
    }
    ClampItemScroll();

    gSbVisible = (N > gListVisible) ? 1 : 0;
    gSbW = SETTINGS_SB_W;
    gSbH = (UINT32)gListVisible * LineH;
    if (gSbH + gListTop > Cy + Ch) {
        gSbH = (Cy + Ch > gListTop) ? (Cy + Ch - gListTop) : 0;
    }
    gSbX = (ListW > SETTINGS_SB_W + 8) ? (ContentX + ListW - SETTINGS_SB_W - 4)
                                      : (ContentX + 4);
    if (gPrevW > 0 && gSbX + gSbW > ContentX + ListW) {
        gSbX = ContentX + 4;
    }
    gSbY = gListTop;
    gListRowW = ListW > 8 ? ListW - 8 : ListW;
    if (gSbVisible && gListRowW > SETTINGS_SB_W + 8) {
        gListRowW -= (SETTINGS_SB_W + 4);
    }

    HalVideoDrawStringAt(ContentX + 8, Cy + 8, CatLabel(gCat), COLOR_BLACK);

    Applied = CurrentItemIndex();
    RowY = gListTop;
    for (i = 0; i < gListVisible && gItemScroll + i < N; i++) {
        int Idx = gItemScroll + i;
        int Mark = (Idx == Applied);
        int Sel = (Idx == gItemSel);
        int Hov = (gHoverKind == 1 && gHoverIdx == Idx);

        ItemLabel(Idx, Label, (int)sizeof(Label));
        Row[0] = Mark ? '*' : ' ';
        Row[1] = ' ';
        {
            int j;
            for (j = 0; Label[j] && j < (int)sizeof(Row) - 3; j++) {
                Row[j + 2] = Label[j];
            }
            Row[j + 2] = 0;
        }
        UiDrawListRow(ContentX + 4, RowY, gListRowW, LineH, Row, Sel, Hov);
        HitAdd(ContentX + 4, RowY, gListRowW, LineH, 1, Idx);
        RowY += LineH;
    }
    if (gSbVisible && gSbH > 0) {
        UiDrawScrollBar(gSbX, gSbY, gSbW, gSbH, gItemScroll, gListVisible, N);
    }

    if (gPrevW > 0) {
        gPrevX = ContentX + ListW;
        DrawDetail(gPrevX, Cy, gPrevW, Ch);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

int SettingsUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_SETTINGS;
}

void SettingsUiRepaint(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
    GuiBackupFocusWindow();
}

void SettingsUiPaintFocused(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    PaintMenu();
}

void SettingsUiOpen(void) {
    gCat = SETTINGS_CAT_DESKTOP;
    gItemSel = CurrentItemIndex();
    gItemScroll = 0;
    gDisplayHint = 0;
    gHoverKind = -1;
    gHoverIdx = -1;
    gPressKind = -1;
    gPressIdx = -1;
    PaintMenu();
    DebugWrite("settings: three-pane open\n");
}

void SettingsUiRefresh(void) {
    int i;

    for (i = 0; i < GUI_MAX_WINS; i++) {
        if (GuiWindowKind(i) != GUI_WIN_SETTINGS) {
            continue;
        }
        GuiRaiseToFront(i);
        GuiPaintWindow(i);
        PaintMenu();
        return;
    }
}

void SettingsUiOnEscape(void) {
    if (!SettingsUiIsFocused()) {
        return;
    }
    /* 三分栏无「返回上级」；Esc 仅刷新 */
    PaintMenu();
}

void SettingsUiOnDigit(char Digit) {
    int N;

    if (!SettingsUiIsFocused()) {
        return;
    }
    if (Digit < '1' || Digit > '9') {
        return;
    }
    N = Digit - '1';
    if (N < ItemCount()) {
        ApplyItem(N);
    }
}

void SettingsUiOnClick(UINT32 X, UINT32 Y) {
    int i;
    int First;

    if (!SettingsUiIsFocused()) {
        return;
    }
    if (gSbVisible &&
        UiScrollBarHit(gSbX, gSbY, gSbW, gSbH, gItemScroll, gListVisible,
                       ItemCount(), X, Y, &First)) {
        gItemScroll = First;
        SettingsUiRepaint();
        return;
    }
    for (i = 0; i < gHitCount; i++) {
        if (!UiHitRect(gHits[i].X, gHits[i].Y, gHits[i].W, gHits[i].H, X, Y)) {
            continue;
        }
        if (gHits[i].Kind == 0) {
            SelectCategory((SETTINGS_CAT)gHits[i].Index);
            SettingsUiRepaint();
            return;
        }
        if (gHits[i].Kind == 1) {
            ApplyItem(gHits[i].Index);
            return;
        }
    }
}

void SettingsUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons) {
    int i;
    int Kind = -1;
    int Idx = -1;
    int Need = 0;
    int FireKind = -1;
    int FireIdx = -1;
    static UINT8 sPrevBtn;

    if (!SettingsUiIsFocused()) {
        if (gHoverKind >= 0 || gPressKind >= 0) {
            gHoverKind = -1;
            gHoverIdx = -1;
            gPressKind = -1;
            gPressIdx = -1;
        }
        sPrevBtn = Buttons;
        return;
    }
    for (i = 0; i < gHitCount; i++) {
        if (UiHitRect(gHits[i].X, gHits[i].Y, gHits[i].W, gHits[i].H, X, Y)) {
            Kind = gHits[i].Kind;
            Idx = gHits[i].Index;
            break;
        }
    }
    if (Kind != gHoverKind || Idx != gHoverIdx) {
        gHoverKind = Kind;
        gHoverIdx = Idx;
        Need = 1;
    }
    if ((Buttons & 1u) && !(sPrevBtn & 1u)) {
        if (Kind >= 0) {
            gPressKind = Kind;
            gPressIdx = Idx;
            Need = 1;
        }
    } else if ((Buttons & 1u) && gPressKind >= 0 &&
               (Kind != gPressKind || Idx != gPressIdx)) {
        gPressKind = -1;
        gPressIdx = -1;
        Need = 1;
    } else if (!(Buttons & 1u) && (sPrevBtn & 1u) && gPressKind >= 0) {
        if (Kind == gPressKind && Idx == gPressIdx) {
            FireKind = gPressKind;
            FireIdx = gPressIdx;
        }
        gPressKind = -1;
        gPressIdx = -1;
        Need = 1;
    }
    sPrevBtn = Buttons;
    if (Need) {
        SettingsUiRepaint();
    }
    if (FireKind == 0) {
        SelectCategory((SETTINGS_CAT)FireIdx);
        SettingsUiRepaint();
    } else if (FireKind == 1) {
        ApplyItem(FireIdx);
    }
}
