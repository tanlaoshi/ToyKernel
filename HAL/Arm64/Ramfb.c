/*
 * Ramfb.c — QEMU fw_cfg MMIO + etc/ramfb（PR-V2）
 *
 * 用选择器 + 数据口按字节访问（避免 DMA 在早期启动卡住）。
 * 配置结构与目录项字段均为 big-endian。四字节格式 XR24（XRGB8888）。
 */
#include "Ramfb.h"
#include "HalSerial.h"

#define FW_CFG_SIGNATURE 0x0000u
#define FW_CFG_FILE_DIR  0x0019u

#define RAMFB_FOURCC_XR24 0x34325258u /* 'XR24' */

typedef struct {
    UINT64 Addr;
    UINT32 Fourcc;
    UINT32 Flags;
    UINT32 Width;
    UINT32 Height;
    UINT32 Stride;
} RAMFB_CFG;

static UINT32 ReadBe32(const void *Addr) {
    const UINT8 *B = (const UINT8 *)Addr;
    return ((UINT32)B[0] << 24) | ((UINT32)B[1] << 16) |
           ((UINT32)B[2] << 8) | (UINT32)B[3];
}

static UINT16 ReadBe16(const void *Addr) {
    const UINT8 *B = (const UINT8 *)Addr;
    return (UINT16)(((UINT16)B[0] << 8) | (UINT16)B[1]);
}

static void StoreBe32(void *Addr, UINT32 V) {
    UINT8 *B = (UINT8 *)Addr;
    B[0] = (UINT8)(V >> 24);
    B[1] = (UINT8)(V >> 16);
    B[2] = (UINT8)(V >> 8);
    B[3] = (UINT8)V;
}

static void StoreBe64(void *Addr, UINT64 V) {
    StoreBe32(Addr, (UINT32)(V >> 32));
    StoreBe32((UINT8 *)Addr + 4, (UINT32)V);
}

static int StrEq56(const char *A, const char *B) {
    int i;
    for (i = 0; i < 56; i++) {
        if (A[i] != B[i]) {
            return 0;
        }
        if (A[i] == 0) {
            return 1;
        }
    }
    return 1;
}

static void FwCfgSelect(UINT64 Base, UINT16 Key) {
    /* 选择器 MMIO 要求 16-bit 访问（BE） */
    *(volatile UINT16 *)(UINTN)(Base + 8) = (UINT16)((Key << 8) | (Key >> 8));
}

static UINT8 FwCfgRead8(UINT64 Base) {
    return *(volatile UINT8 *)(UINTN)Base;
}

static void FwCfgWrite8(UINT64 Base, UINT8 V) {
    *(volatile UINT8 *)(UINTN)Base = V;
}

static void FwCfgReadBytes(UINT64 Base, void *Buf, UINT32 Len) {
    UINT8 *P = (UINT8 *)Buf;
    UINT32 i;
    for (i = 0; i < Len; i++) {
        P[i] = FwCfgRead8(Base);
    }
}

static void FwCfgWriteBytes(UINT64 Base, const void *Buf, UINT32 Len) {
    const UINT8 *P = (const UINT8 *)Buf;
    UINT32 i;
    for (i = 0; i < Len; i++) {
        FwCfgWrite8(Base, P[i]);
    }
}

static int FwCfgFindFile(UINT64 Base, const char *Name, UINT16 *OutSelect,
                         UINT32 *OutSize) {
    UINT8 DirHead[4];
    UINT32 Count;
    UINT32 i;
    UINT8 Entry[64];
    UINT16 Sel;
    UINT32 Size;

    FwCfgSelect(Base, FW_CFG_FILE_DIR);
    FwCfgReadBytes(Base, DirHead, 4);
    Count = ReadBe32(DirHead);
    if (Count > 256) {
        Count = 256;
    }
    for (i = 0; i < Count; i++) {
        FwCfgReadBytes(Base, Entry, 64);
        Size = ReadBe32(Entry);
        Sel = ReadBe16(Entry + 4);
        if (StrEq56((const char *)(Entry + 8), Name)) {
            *OutSelect = Sel;
            *OutSize = Size;
            return 0;
        }
    }
    return -1;
}

int RamfbSetup(BOOT_INFO *Info, UINT64 FwCfgBase, UINT64 *FreeStart,
               UINT64 RamEnd) {
    UINT8 Sig[4];
    UINT16 Sel;
    UINT32 FileSize;
    UINT64 FbBase;
    UINT64 FbBytes;
    UINT64 FbEnd;
    UINT32 W = RAMFB_WIDTH;
    UINT32 H = RAMFB_HEIGHT;
    UINT32 Stride;
    RAMFB_CFG Cfg;
    UINT8 *P;
    UINT64 i;

    if (!Info || !FreeStart || FwCfgBase == 0) {
        return -1;
    }

    FwCfgSelect(FwCfgBase, FW_CFG_SIGNATURE);
    FwCfgReadBytes(FwCfgBase, Sig, 4);
    if (Sig[0] != 'Q' || Sig[1] != 'E' || Sig[2] != 'M' || Sig[3] != 'U') {
        HalSerialWrite("boot: fw_cfg signature missing\n");
        return -1;
    }

    if (FwCfgFindFile(FwCfgBase, "etc/ramfb", &Sel, &FileSize) != 0) {
        HalSerialWrite("boot: etc/ramfb missing (need -device ramfb)\n");
        return -1;
    }

    Stride = W * 4u;
    FbBytes = (UINT64)Stride * (UINT64)H;
    FbBase = (*FreeStart + 0xFFFULL) & ~0xFFFULL;
    FbEnd = FbBase + ((FbBytes + 0xFFFULL) & ~0xFFFULL);
    if (FbEnd > RamEnd || FbEnd < FbBase) {
        HalSerialWrite("boot: ramfb OOM\n");
        return -1;
    }

    P = (UINT8 *)(UINTN)FbBase;
    for (i = 0; i < FbBytes; i++) {
        P[i] = 0;
    }

    StoreBe64(&Cfg.Addr, FbBase);
    StoreBe32(&Cfg.Fourcc, RAMFB_FOURCC_XR24);
    StoreBe32(&Cfg.Flags, 0);
    StoreBe32(&Cfg.Width, W);
    StoreBe32(&Cfg.Height, H);
    StoreBe32(&Cfg.Stride, Stride);

    FwCfgSelect(FwCfgBase, Sel);
    FwCfgWriteBytes(FwCfgBase, &Cfg, (UINT32)sizeof(Cfg));

    Info->FrameBufferBase = FbBase;
    Info->FrameBufferSize = FbEnd - FbBase;
    Info->HorizontalResolution = W;
    Info->VerticalResolution = H;
    Info->PixelsPerScanLine = W;
    *FreeStart = FbEnd;

    HalSerialWrite("boot: ramfb ");
    {
        char Buf[32];
        UINT32 n = W;
        int p = 0;
        char Tmp[16];
        int t = 0;
        if (n == 0) {
            Buf[p++] = '0';
        } else {
            while (n) {
                Tmp[t++] = (char)('0' + (n % 10));
                n /= 10;
            }
            while (t--) {
                Buf[p++] = Tmp[t];
            }
        }
        Buf[p++] = 'x';
        n = H;
        t = 0;
        if (n == 0) {
            Buf[p++] = '0';
        } else {
            while (n) {
                Tmp[t++] = (char)('0' + (n % 10));
                n /= 10;
            }
            while (t--) {
                Buf[p++] = Tmp[t];
            }
        }
        Buf[p] = 0;
        HalSerialWrite(Buf);
    }
    HalSerialWrite(" @");
    {
        static const char Hex[] = "0123456789abcdef";
        char Buf[17];
        UINT64 V = FbBase;
        int k;
        for (k = 15; k >= 0; k--) {
            Buf[k] = Hex[V & 0xf];
            V >>= 4;
        }
        Buf[16] = 0;
        HalSerialWrite(Buf);
    }
    HalSerialWrite("\n");
    return 0;
}
