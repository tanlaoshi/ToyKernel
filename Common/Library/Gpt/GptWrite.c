/*
 * GptWrite.c — 写出课堂 GPT 布局（PR-S-gpt-1）
 */
#include "GptPriv.h"
#include "Block.h"
#include "Debug.h"

/* —— PR-FS-inst-1：写 GPT —— */

static UINT32 GptCrc32(const UINT8 *Data, UINTN Len) {
    UINT32 C = 0xFFFFFFFFu;
    UINTN i;
    int b;

    for (i = 0; i < Len; i++) {
        C ^= Data[i];
        for (b = 0; b < 8; b++) {
            if (C & 1u) {
                C = (C >> 1) ^ 0xEDB88320u;
            } else {
                C >>= 1;
            }
        }
    }
    return ~C;
}

static void ZeroSector(void) {
    int i;
    for (i = 0; i < SECTOR; i++) {
        gGptSector[i] = 0;
    }
}

static void PutGuid(UINT8 *Dst, const UINT8 *Src) {
    int i;
    for (i = 0; i < 16; i++) {
        Dst[i] = Src[i];
    }
}

static void PutUtf16Name(UINT8 *Dst, const char *Ascii) {
    int i;
    for (i = 0; i < 36; i++) {
        Dst[i * 2] = 0;
        Dst[i * 2 + 1] = 0;
    }
    for (i = 0; Ascii[i] && i < 35; i++) {
        Dst[i * 2] = (UINT8)Ascii[i];
        Dst[i * 2 + 1] = 0;
    }
}

static void Write64(UINT8 *P, UINT64 V) {
    int i;
    for (i = 0; i < 8; i++) {
        P[i] = (UINT8)((V >> (8 * i)) & 0xFF);
    }
}


static void Write32Le(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)(V & 0xFF);
    P[1] = (UINT8)((V >> 8) & 0xFF);
    P[2] = (UINT8)((V >> 16) & 0xFF);
    P[3] = (UINT8)((V >> 24) & 0xFF);
}

int GptWriteToyLayout(UINT64 TotalSectors, UINT32 EspMib,
                      UINT32 *OutEspLba, UINT32 *OutEspSectors,
                      UINT32 *OutToyLba, UINT32 *OutToySectors) {
    UINT32 EspLba = 2048; /* 1MiB */
    UINT32 EspSectors;
    UINT32 ToyLba;
    UINT32 ToySectors;
    UINT64 FirstUsable = 34;
    UINT64 LastUsable;
    UINT64 BackupLba;
    UINT64 EntriesLba = 2;
    UINT64 EntriesBackupLba;
    static UINT8 Entries[32 * SECTOR];
    static UINT8 Hdr[SECTOR];
    UINT32 EntriesCrc;
    UINT32 HdrCrc;
    UINT32 i;
    static const UINT8 DiskGuid[16] = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
    };
    static const UINT8 EspPartGuid[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C
    };
    static const UINT8 ToyPartGuid[16] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0
    };

    if (EspMib < 32) {
        EspMib = 256;
    }
    if (TotalSectors < (UINT64)(EspLba + EspMib * 2048ull + 2048ull + 34ull)) {
        DebugWrite("gpt: disk too small\n");
        return 0;
    }
    DebugWrite("gpt: total="); DebugHex32((UINT32)TotalSectors); DebugWrite("\n");
    EspSectors = EspMib * 2048u;
    ToyLba = EspLba + EspSectors;
    LastUsable = TotalSectors - 34ull;
    if ((UINT64)ToyLba >= LastUsable) {
        return 0;
    }
    ToySectors = (UINT32)(LastUsable - (UINT64)ToyLba + 1ull);
    BackupLba = TotalSectors - 1ull;
    EntriesBackupLba = TotalSectors - 33ull;

    /* Protective MBR */
    ZeroSector();
    gGptSector[0x1BE] = 0x00;
    gGptSector[0x1BE + 1] = 0x00;
    gGptSector[0x1BE + 2] = 0x02;
    gGptSector[0x1BE + 3] = 0x00;
    gGptSector[0x1BE + 4] = 0xEE;
    gGptSector[0x1BE + 5] = 0xFF;
    gGptSector[0x1BE + 6] = 0xFF;
    gGptSector[0x1BE + 7] = 0xFF;
    Write32Le(gGptSector + 0x1BE + 8, 1);
    Write32Le(gGptSector + 0x1BE + 12, (UINT32)(TotalSectors > 0xFFFFFFFFull ? 0xFFFFFFFFu : (TotalSectors - 1ull)));
    gGptSector[510] = 0x55;
    gGptSector[511] = 0xAA;
    if (!BlockWriteSectors(0, 1, gGptSector)) {
        DebugWrite("gpt: MBR write fail\n");
        return 0;
    }

    /* Partition entries (2 × 128B used; rest zero) */
    for (i = 0; i < sizeof(Entries); i++) {
        Entries[i] = 0;
    }
    PutGuid(Entries + 0, EFI_PART);
    PutGuid(Entries + 16, EspPartGuid);
    Write64(Entries + 32, EspLba);
    Write64(Entries + 40, (UINT64)EspLba + EspSectors - 1ull);
    PutUtf16Name(Entries + 56, "ESP");

    PutGuid(Entries + 128, BASIC_DATA);
    PutGuid(Entries + 128 + 16, ToyPartGuid);
    Write64(Entries + 128 + 32, ToyLba);
    Write64(Entries + 128 + 40, (UINT64)ToyLba + ToySectors - 1ull);
    PutUtf16Name(Entries + 128 + 56, "TOYOS");

    EntriesCrc = GptCrc32(Entries, sizeof(Entries));
    for (i = 0; i < 32; i++) {
        if (!BlockWriteSectors((UINT32)(EntriesLba + i), 1, Entries + i * SECTOR)) {
            DebugWrite("gpt: entries write fail\n");
            return 0;
        }
    }
    for (i = 0; i < 32; i++) {
        if (!BlockWriteSectors((UINT32)(EntriesBackupLba + i), 1, Entries + i * SECTOR)) {
            DebugWrite("gpt: backup entries fail\n");
            return 0;
        }
    }

    /* Primary header LBA1 */
    for (i = 0; i < SECTOR; i++) {
        Hdr[i] = 0;
    }
    /* Signature "EFI PART" */
    Hdr[0] = 'E'; Hdr[1] = 'F'; Hdr[2] = 'I'; Hdr[3] = ' ';
    Hdr[4] = 'P'; Hdr[5] = 'A'; Hdr[6] = 'R'; Hdr[7] = 'T';
    Write32Le(Hdr + 8, 0x00010000);
    Write32Le(Hdr + 12, 92);
    Write32Le(Hdr + 16, 0); /* CRC placeholder */
    Write64(Hdr + 24, 1);
    Write64(Hdr + 32, BackupLba);
    Write64(Hdr + 40, FirstUsable);
    Write64(Hdr + 48, LastUsable);
    PutGuid(Hdr + 56, DiskGuid);
    Write64(Hdr + 72, EntriesLba);
    Write32Le(Hdr + 80, 128);
    Write32Le(Hdr + 84, 128);
    Write32Le(Hdr + 88, EntriesCrc);
    HdrCrc = GptCrc32(Hdr, 92);
    Write32Le(Hdr + 16, HdrCrc);
    if (!BlockWriteSectors(1, 1, Hdr)) {
        DebugWrite("gpt: primary hdr fail\n");
        return 0;
    }

    /* Backup header */
    Write32Le(Hdr + 16, 0);
    Write64(Hdr + 24, BackupLba);
    Write64(Hdr + 32, 1);
    Write64(Hdr + 72, EntriesBackupLba);
    HdrCrc = GptCrc32(Hdr, 92);
    Write32Le(Hdr + 16, HdrCrc);
    if (!BlockWriteSectors((UINT32)BackupLba, 1, Hdr)) {
        DebugWrite("gpt: backup hdr fail\n");
        return 0;
    }

    if (OutEspLba) {
        *OutEspLba = EspLba;
    }
    if (OutEspSectors) {
        *OutEspSectors = EspSectors;
    }
    if (OutToyLba) {
        *OutToyLba = ToyLba;
    }
    if (OutToySectors) {
        *OutToySectors = ToySectors;
    }
    DebugWrite("gpt: wrote ESP@");
    DebugHex32(EspLba);
    DebugWrite(" TOYOS@");
    DebugHex32(ToyLba);
    DebugWrite("\n");
    return 1;
}
