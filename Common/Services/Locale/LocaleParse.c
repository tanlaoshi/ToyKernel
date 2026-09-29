/*
 * LocaleParse.c — PR-S3-locale-1：catalog 文本解析与装载
 */
#include "LocalePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "Debug.h"

static void CopyStrUnescape(char *Dst, UINTN DstMax, const char *Src) {
    UINTN i;
    UINTN o;

    if (!Dst || DstMax == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    o = 0;
    for (i = 0; Src[i] && o + 1 < DstMax; i++) {
        if (Src[i] == '\\' && Src[i + 1] == 'n') {
            Dst[o++] = '\n';
            i++;
        } else if (Src[i] == '\\' && Src[i + 1] == '\\') {
            Dst[o++] = '\\';
            i++;
        } else {
            Dst[o++] = Src[i];
        }
    }
    Dst[o] = 0;
}

static void ResetToFallback(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        CopyStrUnescape(gEn[i], LOCALE_STR_MAX, gEnFallback[i]);
        CopyStrUnescape(gZh[i], LOCALE_STR_MAX, gZhFallback[i]);
    }
}

static int KeyToId(const char *Key) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        const char *K = gMsgKeys[i];
        UINTN j;
        for (j = 0; K[j] && Key[j] && K[j] == Key[j]; j++) {
        }
        if (K[j] == 0 && Key[j] == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void ApplyLine(char (*Table)[LOCALE_STR_MAX], const char *Line) {
    char Key[48];
    const char *Eq;
    const char *Val;
    UINTN i;
    int Id;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#' || *Line == ';') {
        return;
    }
    Eq = Line;
    while (*Eq && *Eq != '=') {
        Eq++;
    }
    if (*Eq != '=' || Eq == Line) {
        return;
    }
    i = 0;
    while (Line < Eq && i + 1 < sizeof(Key)) {
        if (*Line != ' ' && *Line != '\t') {
            Key[i++] = *Line;
        }
        Line++;
    }
    Key[i] = 0;
    Val = Eq + 1;
    while (*Val == ' ' || *Val == '\t') {
        Val++;
    }
    Id = KeyToId(Key);
    if (Id < 0) {
        return;
    }
    CopyStrUnescape(Table[Id], LOCALE_STR_MAX, Val);
}

static int LoadCatalogFile(const char *Path, char (*Table)[LOCALE_STR_MAX]) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN i;
    UINTN LineStart;
    int Err;

    Pages = (LOCALE_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, LOCALE_FILE_MAX - 1, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Buf[Size] = 0;
    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            char Saved = (char)Buf[i];
            Buf[i] = 0;
            if (i > LineStart) {
                ApplyLine(Table, (const char *)&Buf[LineStart]);
            }
            Buf[i] = (UINT8)Saved;
            if (i < Size && Buf[i] == '\r' && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    return 0;
}

void LocaleLoadCatalogs(void) {
    int EnOk;
    int ZhOk;

    ResetToFallback();
    EnOk = LoadCatalogFile("Assets/Locale/en.txt", gEn);
    ZhOk = LoadCatalogFile("Assets/Locale/zh.txt", gZh);
    if (EnOk == 0 || ZhOk == 0) {
        DebugWrite("locale: catalog ");
        DebugWrite(EnOk == 0 ? "en " : "");
        DebugWrite(ZhOk == 0 ? "zh " : "");
        DebugWrite("from Assets/Locale\n");
    } else {
        DebugWrite("locale: using built-in strings (no Assets/Locale)\n");
    }
}
