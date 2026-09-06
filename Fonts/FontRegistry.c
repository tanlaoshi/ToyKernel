/*
 * Fonts/FontRegistry.c — 字体注册表与当前字体（PR-D1）+ Assets/Fonts TOYF（PR-T3）
 *
 * TOYF v1（小端）：
 *   0  magic "TOYF"
 *   4  version=1, scale, char_spacing, line_spacing  (4×u8)
 *   8  width, height, first_char, glyph_count         (4×u16 LE)
 *  16  name[16] NUL 填充
 *  32  glyph bytes = glyph_count * height * ((width+7)/8)
 */
#include "Font.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "HalConsole.h"

#define FONT_TOYF_MAGIC   0x46594F54u /* 'TOYF' LE */
#define FONT_TOYF_VERSION 1u
#define FONT_TOYF_HDR     32u
#define FONT_FILE_MAX     (64u * 1024u)
#define FONT_RUNTIME_MAX  2u
#define FONT_NAME_MAX     16u
#define FONT_TABLE_MAX    8u

typedef struct FONT_RUNTIME_SLOT {
    FONT_FACE Face;
    char Name[FONT_NAME_MAX];
    UINT8 *Pages;
    UINT32 PageCount;
    int Used;
} FONT_RUNTIME_SLOT;

static FONT_RUNTIME_SLOT gRuntime[FONT_RUNTIME_MAX];
static const FONT_FACE *gFonts[FONT_TABLE_MAX];
static UINT32 gFontCount;
static UINT32 gCurrentId;

static UINT16 RdU16(const UINT8 *P) {
    return (UINT16)P[0] | ((UINT16)P[1] << 8);
}

static UINT32 RdU32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
           ((UINT32)P[3] << 24);
}

static void RebuildFontTable(void) {
    UINT32 i;

    gFontCount = 0;
    gFonts[gFontCount++] = &gFontFaceTerminus16x32;
    gFonts[gFontCount++] = &gFontFaceTerminusX2;
    gFonts[gFontCount++] = &gFontFaceTerminus10x18;
    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        if (gRuntime[i].Used && gFontCount < FONT_TABLE_MAX) {
            gFonts[gFontCount++] = &gRuntime[i].Face;
        }
    }
}

static void FreeRuntimeSlot(FONT_RUNTIME_SLOT *S) {
    if (S->Pages && S->PageCount) {
        PhysicalMemoryFreePages(S->Pages, S->PageCount);
    }
    S->Pages = 0;
    S->PageCount = 0;
    S->Used = 0;
    S->Face.Glyphs = 0;
    S->Face.Name = S->Name;
}

static void ClearRuntime(void) {
    UINT32 i;

    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        FreeRuntimeSlot(&gRuntime[i]);
    }
    RebuildFontTable();
}

static int ParseToyf(const UINT8 *Buf, UINTN Size, FONT_RUNTIME_SLOT *Out) {
    UINT32 Magic;
    UINT32 Width;
    UINT32 Height;
    UINT32 First;
    UINT32 Count;
    UINT32 Bpr;
    UINT32 GlyphBytes;
    UINT32 Need;
    UINT32 i;
    UINT8 Scale;
    UINT8 CharSp;
    UINT8 LineSp;

    if (Size < FONT_TOYF_HDR) {
        return -1;
    }
    Magic = RdU32(Buf);
    if (Magic != FONT_TOYF_MAGIC || Buf[4] != FONT_TOYF_VERSION) {
        return -1;
    }
    Scale = Buf[5];
    CharSp = Buf[6];
    LineSp = Buf[7];
    Width = RdU16(Buf + 8);
    Height = RdU16(Buf + 10);
    First = RdU16(Buf + 12);
    Count = RdU16(Buf + 14);
    if (Scale == 0) {
        Scale = 1;
    }
    if (Width == 0 || Width > 64 || Height == 0 || Height > 64 || Count == 0 ||
        Count > 256) {
        return -1;
    }
    Bpr = (Width + 7u) / 8u;
    GlyphBytes = Count * Height * Bpr;
    Need = FONT_TOYF_HDR + GlyphBytes;
    if (Size < Need) {
        return -1;
    }

    FreeRuntimeSlot(Out);
    Out->PageCount = (GlyphBytes + 4095u) / 4096u;
    if (Out->PageCount == 0) {
        Out->PageCount = 1;
    }
    Out->Pages = (UINT8 *)PhysicalMemoryAllocatePages(Out->PageCount);
    if (!Out->Pages) {
        Out->PageCount = 0;
        return -1;
    }
    for (i = 0; i < GlyphBytes; i++) {
        Out->Pages[i] = Buf[FONT_TOYF_HDR + i];
    }

    for (i = 0; i < FONT_NAME_MAX; i++) {
        Out->Name[i] = (char)Buf[16 + i];
    }
    Out->Name[FONT_NAME_MAX - 1] = 0;
    if (Out->Name[0] == 0) {
        Out->Name[0] = 'F';
        Out->Name[1] = 'N';
        Out->Name[2] = 'T';
        Out->Name[3] = 0;
    }

    Out->Face.Name = Out->Name;
    Out->Face.Width = Width;
    Out->Face.Height = Height;
    Out->Face.BytesPerGlyph = Height * Bpr;
    Out->Face.BytesPerRow = Bpr;
    Out->Face.CharSpacing = CharSp;
    Out->Face.LineSpacing = LineSp;
    Out->Face.Scale = Scale;
    Out->Face.Glyphs = Out->Pages;
    Out->Face.GlyphCount = Count;
    Out->Face.FirstChar = First;
    Out->Used = 1;
    return 0;
}

static int TryLoadPath(const char *Path, FONT_RUNTIME_SLOT *Slot) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    int Err;

    Pages = (FONT_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, FONT_FILE_MAX, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Err = ParseToyf(Buf, Size, Slot);
    PhysicalMemoryFreePages(Buf, Pages);
    if (Err != 0) {
        HalConsoleWriteSerial("font: bad TOYF ");
        HalConsoleWriteSerial(Path);
        HalConsoleWriteSerial("\n");
        return -1;
    }
    HalConsoleWriteSerial("font: loaded ");
    HalConsoleWriteSerial(Path);
    HalConsoleWriteSerial(" (");
    HalConsoleWriteSerial(Slot->Name);
    HalConsoleWriteSerial(")\n");
    return 0;
}

void FontInit(void) {
    UINT32 i;

    gCurrentId = 0;
    for (i = 0; i < FONT_RUNTIME_MAX; i++) {
        gRuntime[i].Used = 0;
        gRuntime[i].Pages = 0;
        gRuntime[i].PageCount = 0;
        gRuntime[i].Face.Name = gRuntime[i].Name;
    }
    RebuildFontTable();
}

int FontLoadAssets(void) {
    static const char *const Paths[] = {
        "Assets/Fonts/VGA8X16.FNT",
        "Assets/Fonts/EXTRA.FNT",
    };
    UINT32 Slot;
    UINT32 i;
    int Any = 0;

    ClearRuntime();
    Slot = 0;
    for (i = 0; i < sizeof(Paths) / sizeof(Paths[0]) && Slot < FONT_RUNTIME_MAX;
         i++) {
        if (TryLoadPath(Paths[i], &gRuntime[Slot]) == 0) {
            Slot++;
            Any = 1;
        }
    }
    RebuildFontTable();
    if (!Any) {
        HalConsoleWriteSerial("font: using built-in faces (no Assets/Fonts TOYF)\n");
    }
    return Any ? 0 : -1;
}

int FontReloadAssets(void) {
    UINT32 Cur = gCurrentId;

    (void)FontLoadAssets();
    if (FontSetById(Cur) != 0) {
        (void)FontSetById(0);
    }
    return 0;
}

UINT32 FontCount(void) {
    return gFontCount;
}

UINT32 FontCurrentId(void) {
    return gCurrentId;
}

const FONT_FACE *FontGetById(UINT32 Id) {
    if (Id >= gFontCount) {
        return 0;
    }
    return gFonts[Id];
}

const FONT_FACE *FontGetCurrent(void) {
    const FONT_FACE *F = FontGetById(gCurrentId);
    if (F == 0) {
        return &gFontFaceTerminus16x32;
    }
    return F;
}

int FontSetById(UINT32 Id) {
    if (Id >= gFontCount) {
        return -1;
    }
    gCurrentId = Id;
    return 0;
}

static UINT32 ScaleOf(const FONT_FACE *F) {
    return F->Scale ? F->Scale : 1u;
}

UINT32 FontCellW(void) {
    const FONT_FACE *F = FontGetCurrent();
    return F->Width * ScaleOf(F);
}

UINT32 FontCellH(void) {
    const FONT_FACE *F = FontGetCurrent();
    return F->Height * ScaleOf(F);
}

UINT32 FontAdvanceX(void) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 S = ScaleOf(F);
    return F->Width * S + F->CharSpacing * S;
}

UINT32 FontAdvanceY(void) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 S = ScaleOf(F);
    return F->Height * S + F->LineSpacing * S;
}

const UINT8 *FontGlyph(char C) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 U = (UINT32)(UINT8)C;

    if (F == 0 || F->Glyphs == 0 || F->BytesPerGlyph == 0) {
        return 0;
    }
    if (U < F->FirstChar || U >= F->FirstChar + F->GlyphCount) {
        return 0;
    }
    return F->Glyphs + (U - F->FirstChar) * F->BytesPerGlyph;
}

UINTN Utf8Decode(const char *S, UINT32 *OutCp) {
    UINT8 C0;
    UINT32 Cp;
    UINTN Need;
    UINTN i;

    if (!S || !OutCp) {
        return 0;
    }
    C0 = (UINT8)S[0];
    if (C0 == 0) {
        return 0;
    }
    if (C0 < 0x80) {
        *OutCp = C0;
        return 1;
    }
    if ((C0 & 0xE0) == 0xC0) {
        Cp = C0 & 0x1F;
        Need = 2;
    } else if ((C0 & 0xF0) == 0xE0) {
        Cp = C0 & 0x0F;
        Need = 3;
    } else if ((C0 & 0xF8) == 0xF0) {
        Cp = C0 & 0x07;
        Need = 4;
    } else {
        return 0;
    }
    for (i = 1; i < Need; i++) {
        UINT8 Cx = (UINT8)S[i];
        if ((Cx & 0xC0) != 0x80) {
            return 0;
        }
        Cp = (Cp << 6) | (UINT32)(Cx & 0x3F);
    }
    *OutCp = Cp;
    return Need;
}

const UINT8 *FontGlyphCp(UINT32 Cp, UINT32 *OutW, UINT32 *OutH) {
    const FONT_FACE *F;
    const UINT8 *G;

    if (Cp >= 32 && Cp < 127) {
        F = FontGetCurrent();
        G = FontGlyph((char)Cp);
        if (!G || !F) {
            return 0;
        }
        if (OutW) {
            *OutW = F->Width;
        }
        if (OutH) {
            *OutH = F->Height;
        }
        return G;
    }
    return FontCjk16Lookup(Cp, OutW, OutH);
}

UINT32 FontGlyphStretch(UINT32 GlyphH) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 FaceScale = (F && F->Scale) ? F->Scale : 1u;
    UINT32 CellH = FontCellH();

    if (GlyphH == 0) {
        return FaceScale;
    }
    if (GlyphH * FaceScale >= CellH) {
        return FaceScale;
    }
    return CellH / GlyphH;
}

UINT32 FontCodepointAdvance(UINT32 Cp) {
    UINT32 W;
    UINT32 H;
    UINT32 Scale;

    if (Cp < 128) {
        return FontAdvanceX();
    }
    if (!FontGlyphCp(Cp, &W, &H)) {
        return FontAdvanceX();
    }
    Scale = FontGlyphStretch(H);
    W = W * Scale;
    if (W < FontAdvanceX()) {
        W = FontAdvanceX();
    }
    return W;
}

UINT32 FontStringWidth(const char *S) {
    UINT32 Total = 0;

    while (S && *S) {
        UINT32 Cp;
        UINTN N;

        if (*S == '\n') {
            break;
        }
        N = Utf8Decode(S, &Cp);
        if (N == 0) {
            S++;
            continue;
        }
        Total += FontCodepointAdvance(Cp);
        S += N;
    }
    return Total;
}
