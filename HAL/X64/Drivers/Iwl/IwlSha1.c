/*
 * IwlSha1.c — SHA-1 and HMAC-SHA1 (teaching-minimal, no libc)
 */
#include "IwlPrivate.h"

static UINT32 Rol(UINT32 X, UINTN N)
{
    return (X << N) | (X >> (32u - N));
}

static void Sha1Block(UINT32 H[5], const UINT8 Block[64])
{
    UINT32 W[80];
    UINT32 A, B, C, D, E, F, K, T;
    UINTN I;

    for (I = 0; I < 16; I++) {
        W[I] = ((UINT32)Block[I * 4] << 24) |
               ((UINT32)Block[I * 4 + 1] << 16) |
               ((UINT32)Block[I * 4 + 2] << 8) |
               (UINT32)Block[I * 4 + 3];
    }
    for (I = 16; I < 80; I++) {
        W[I] = Rol(W[I - 3] ^ W[I - 8] ^ W[I - 14] ^ W[I - 16], 1);
    }

    A = H[0]; B = H[1]; C = H[2]; D = H[3]; E = H[4];
    for (I = 0; I < 80; I++) {
        if (I < 20) {
            F = (B & C) | ((~B) & D);
            K = 0x5A827999u;
        } else if (I < 40) {
            F = B ^ C ^ D;
            K = 0x6ED9EBA1u;
        } else if (I < 60) {
            F = (B & C) | (B & D) | (C & D);
            K = 0x8F1BBCDCu;
        } else {
            F = B ^ C ^ D;
            K = 0xCA62C1D6u;
        }
        T = Rol(A, 5) + F + E + K + W[I];
        E = D; D = C; C = Rol(B, 30); B = A; A = T;
    }
    H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E;
}

static void Sha1Init(UINT32 H[5])
{
    H[0] = 0x67452301u;
    H[1] = 0xEFCDAB89u;
    H[2] = 0x98BADCFEu;
    H[3] = 0x10325476u;
    H[4] = 0xC3D2E1F0u;
}

static void Sha1Finish(UINT32 H[5], UINT8 Block[64], UINTN Used, UINT64 TotalLen)
{
    UINT64 BitLen;
    UINTN I;

    Block[Used++] = 0x80u;
    if (Used > 56) {
        while (Used < 64) {
            Block[Used++] = 0;
        }
        Sha1Block(H, Block);
        Used = 0;
    }
    while (Used < 56) {
        Block[Used++] = 0;
    }
    BitLen = TotalLen * 8u;
    Block[56] = (UINT8)(BitLen >> 56);
    Block[57] = (UINT8)(BitLen >> 48);
    Block[58] = (UINT8)(BitLen >> 40);
    Block[59] = (UINT8)(BitLen >> 32);
    Block[60] = (UINT8)(BitLen >> 24);
    Block[61] = (UINT8)(BitLen >> 16);
    Block[62] = (UINT8)(BitLen >> 8);
    Block[63] = (UINT8)BitLen;
    Sha1Block(H, Block);
    for (I = 0; I < 5; I++) {
        Block[I * 4]     = (UINT8)(H[I] >> 24);
        Block[I * 4 + 1] = (UINT8)(H[I] >> 16);
        Block[I * 4 + 2] = (UINT8)(H[I] >> 8);
        Block[I * 4 + 3] = (UINT8)H[I];
    }
}

void IwlSha1(const UINT8 *Data, UINTN Len, UINT8 Out[20])
{
    UINT32 H[5];
    UINT8 Block[64];
    UINTN Off;
    UINTN I;
    UINTN N;

    Sha1Init(H);
    Off = 0;
    while (Off + 64 <= Len) {
        Sha1Block(H, Data + Off);
        Off += 64;
    }
    N = Len - Off;
    for (I = 0; I < 64; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Off + I];
    }
    Sha1Finish(H, Block, N, Len);
    for (I = 0; I < 20; I++) {
        Out[I] = Block[I];
    }
}

void IwlHmacSha1(const UINT8 *Key, UINTN KeyLen,
                 const UINT8 *Data, UINTN Len, UINT8 Out[20])
{
    UINT8 Kpad[64];
    UINT8 Tk[20];
    UINT8 Inner[20];
    UINT8 Outer[84];
    UINT32 H[5];
    UINT8 Block[64];
    UINTN I;
    UINTN Off;
    UINTN N;
    const UINT8 *Kuse;
    UINTN Klen;

    if (KeyLen > 64) {
        IwlSha1(Key, KeyLen, Tk);
        Kuse = Tk;
        Klen = 20;
    } else {
        Kuse = Key;
        Klen = KeyLen;
    }
    for (I = 0; I < 64; I++) {
        Kpad[I] = 0;
    }
    for (I = 0; I < Klen; I++) {
        Kpad[I] = Kuse[I];
    }

    /* inner = SHA1((K ^ ipad) || Data) */
    Sha1Init(H);
    for (I = 0; I < 64; I++) {
        Block[I] = (UINT8)(Kpad[I] ^ 0x36u);
    }
    Sha1Block(H, Block);
    Off = 0;
    while (Off + 64 <= Len) {
        Sha1Block(H, Data + Off);
        Off += 64;
    }
    N = Len - Off;
    for (I = 0; I < 64; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Off + I];
    }
    Sha1Finish(H, Block, N, 64u + Len);
    for (I = 0; I < 20; I++) {
        Inner[I] = Block[I];
    }

    for (I = 0; I < 64; I++) {
        Outer[I] = (UINT8)(Kpad[I] ^ 0x5cu);
    }
    for (I = 0; I < 20; I++) {
        Outer[64 + I] = Inner[I];
    }
    IwlSha1(Outer, 84, Out);
}
