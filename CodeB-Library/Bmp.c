/*
 * Bmp.c — BI_RGB BMP → RGB888（PR-G13）
 */
#include "Bmp.h"
#include "PhysicalMemory.h"

void BmpFree(BMP_IMAGE *Img) {
    if (!Img) {
        return;
    }
    if (Img->Pixels && Img->Pages) {
        PhysicalMemoryFreePages(Img->Pixels, Img->Pages);
    }
    Img->Pixels = 0;
    Img->Width = 0;
    Img->Height = 0;
    Img->Pages = 0;
}

int BmpDecode(const void *Data, UINTN Size, BMP_IMAGE *Out) {
    const UINT8 *P;
    UINT16 Type;
    UINT32 OffBits;
    UINT32 InfoSize;
    INT32 Width;
    INT32 Height;
    UINT16 Planes;
    UINT16 BitCount;
    UINT32 Compression;
    UINT32 W;
    UINT32 H;
    int TopDown;
    UINT32 RowStride;
    UINT64 PixBytes;
    UINT32 Pages;
    UINT32 *Dst;
    UINT32 Y;
    UINT32 X;
    const UINT8 *SrcRow;

    if (!Data || !Out || Size < 54) {
        return -1;
    }
    Out->Pixels = 0;
    Out->Width = 0;
    Out->Height = 0;
    Out->Pages = 0;

    P = (const UINT8 *)Data;
    Type = (UINT16)(P[0] | (P[1] << 8));
    OffBits = (UINT32)(P[10] | (P[11] << 8) | (P[12] << 16) | (P[13] << 24));
    if (Type != 0x4D42 || OffBits >= Size) {
        return -1;
    }

    P = (const UINT8 *)Data + 14;
    InfoSize = (UINT32)(P[0] | (P[1] << 8) | (P[2] << 16) | (P[3] << 24));
    Width = (INT32)(P[4] | (P[5] << 8) | (P[6] << 16) | (P[7] << 24));
    Height = (INT32)(P[8] | (P[9] << 8) | (P[10] << 16) | (P[11] << 24));
    Planes = (UINT16)(P[12] | (P[13] << 8));
    BitCount = (UINT16)(P[14] | (P[15] << 8));
    Compression = (UINT32)(P[16] | (P[17] << 8) | (P[18] << 16) | (P[19] << 24));

    if (InfoSize < 40 || Planes != 1 || Compression != 0) {
        return -1;
    }
    if (BitCount != 24 && BitCount != 32) {
        return -1;
    }
    if (Width <= 0 || Width > 4096) {
        return -1;
    }
    TopDown = 0;
    if (Height < 0) {
        TopDown = 1;
        H = (UINT32)(-Height);
    } else {
        H = (UINT32)Height;
    }
    if (H == 0 || H > 4096) {
        return -1;
    }
    W = (UINT32)Width;
    RowStride = ((W * (BitCount / 8) + 3u) & ~3u);
    if ((UINT64)OffBits + (UINT64)RowStride * H > Size) {
        return -1;
    }

    PixBytes = (UINT64)W * (UINT64)H * sizeof(UINT32);
    Pages = (UINT32)((PixBytes + 4095ull) / 4096ull);
    if (Pages == 0) {
        return -1;
    }
    Dst = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (!Dst) {
        return -1;
    }

    for (Y = 0; Y < H; Y++) {
        UINT32 SrcY = TopDown ? Y : (H - 1 - Y);
        SrcRow = (const UINT8 *)Data + OffBits + SrcY * RowStride;
        for (X = 0; X < W; X++) {
            UINT32 B;
            UINT32 G;
            UINT32 R;
            if (BitCount == 24) {
                B = SrcRow[X * 3 + 0];
                G = SrcRow[X * 3 + 1];
                R = SrcRow[X * 3 + 2];
            } else {
                B = SrcRow[X * 4 + 0];
                G = SrcRow[X * 4 + 1];
                R = SrcRow[X * 4 + 2];
            }
            Dst[Y * W + X] = (R << 16) | (G << 8) | B;
        }
    }

    Out->Pixels = Dst;
    Out->Width = W;
    Out->Height = H;
    Out->Pages = Pages;
    return 0;
}
