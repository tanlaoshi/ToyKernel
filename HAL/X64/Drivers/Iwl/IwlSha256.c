/*
 * IwlSha256.c — SHA-256 与 802.11 KDF（描述符版本 3 的 PTK）
 */
#include "IwlPrivate.h"

static UINT32 Rotr(UINT32 X, UINTN N)
{
    return (X >> N) | (X << (32u - N));
}

static const UINT32 gK[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void Sha256Block(UINT32 S[8], const UINT8 Block[64])
{
    UINT32 W[64];
    UINT32 A, B, C, D, E, F, G, H;
    UINTN I;

    for (I = 0; I < 16; I++) {
        W[I] = ((UINT32)Block[I * 4] << 24) | ((UINT32)Block[I * 4 + 1] << 16)
             | ((UINT32)Block[I * 4 + 2] << 8) | (UINT32)Block[I * 4 + 3];
    }
    for (I = 16; I < 64; I++) {
        UINT32 S0 = Rotr(W[I - 15], 7) ^ Rotr(W[I - 15], 18) ^ (W[I - 15] >> 3);
        UINT32 S1 = Rotr(W[I - 2], 17) ^ Rotr(W[I - 2], 19) ^ (W[I - 2] >> 10);
        W[I] = W[I - 16] + S0 + W[I - 7] + S1;
    }
    A = S[0]; B = S[1]; C = S[2]; D = S[3];
    E = S[4]; F = S[5]; G = S[6]; H = S[7];
    for (I = 0; I < 64; I++) {
        UINT32 S1 = Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25);
        UINT32 Ch = (E & F) ^ ((~E) & G);
        UINT32 T1 = H + S1 + Ch + gK[I] + W[I];
        UINT32 S0 = Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22);
        UINT32 Maj = (A & B) ^ (A & C) ^ (B & C);
        UINT32 T2 = S0 + Maj;
        H = G; G = F; F = E; E = D + T1; D = C; C = B; B = A; A = T1 + T2;
    }
    S[0] += A; S[1] += B; S[2] += C; S[3] += D;
    S[4] += E; S[5] += F; S[6] += G; S[7] += H;
}

static void Sha256Init(UINT32 S[8])
{
    S[0] = 0x6a09e667u; S[1] = 0xbb67ae85u; S[2] = 0x3c6ef372u; S[3] = 0xa54ff53au;
    S[4] = 0x510e527fu; S[5] = 0x9b05688cu; S[6] = 0x1f83d9abu; S[7] = 0x5be0cd19u;
}

static void Sha256Finish(UINT32 S[8], UINT8 Block[64], UINTN Used, UINT64 TotalLen)
{
    UINT64 Bits;
    UINTN I;

    Block[Used++] = 0x80u;
    if (Used > 56) {
        while (Used < 64) {
            Block[Used++] = 0;
        }
        Sha256Block(S, Block);
        Used = 0;
    }
    while (Used < 56) {
        Block[Used++] = 0;
    }
    Bits = TotalLen * 8u;
    Block[56] = (UINT8)(Bits >> 56);
    Block[57] = (UINT8)(Bits >> 48);
    Block[58] = (UINT8)(Bits >> 40);
    Block[59] = (UINT8)(Bits >> 32);
    Block[60] = (UINT8)(Bits >> 24);
    Block[61] = (UINT8)(Bits >> 16);
    Block[62] = (UINT8)(Bits >> 8);
    Block[63] = (UINT8)Bits;
    Sha256Block(S, Block);
    for (I = 0; I < 8; I++) {
        Block[I * 4]     = (UINT8)(S[I] >> 24);
        Block[I * 4 + 1] = (UINT8)(S[I] >> 16);
        Block[I * 4 + 2] = (UINT8)(S[I] >> 8);
        Block[I * 4 + 3] = (UINT8)S[I];
    }
}

static void Sha256(const UINT8 *Data, UINTN Len, UINT8 Out[32])
{
    UINT32 S[8];
    UINT8 Block[64];
    UINTN Off;
    UINTN I;
    UINTN N;

    Sha256Init(S);
    Off = 0;
    while (Off + 64 <= Len) {
        Sha256Block(S, Data + Off);
        Off += 64;
    }
    N = Len - Off;
    for (I = 0; I < 64; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Off + I];
    }
    Sha256Finish(S, Block, N, Len);
    for (I = 0; I < 32; I++) {
        Out[I] = Block[I];
    }
}

static void HmacSha256(const UINT8 *Key, UINTN KeyLen,
                       const UINT8 *Data, UINTN Len, UINT8 Out[32])
{
    UINT8 Kpad[64];
    UINT8 Tk[32];
    UINT8 Inner[32];
    UINT8 Outer[96];
    UINT32 S[8];
    UINT8 Block[64];
    UINTN I;
    UINTN Off;
    UINTN N;
    const UINT8 *Kuse;
    UINTN Klen;

    if (KeyLen > 64) {
        Sha256(Key, KeyLen, Tk);
        Kuse = Tk;
        Klen = 32;
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
    Sha256Init(S);
    for (I = 0; I < 64; I++) {
        Block[I] = (UINT8)(Kpad[I] ^ 0x36u);
    }
    Sha256Block(S, Block);
    Off = 0;
    while (Off + 64 <= Len) {
        Sha256Block(S, Data + Off);
        Off += 64;
    }
    N = Len - Off;
    for (I = 0; I < 64; I++) {
        Block[I] = 0;
    }
    for (I = 0; I < N; I++) {
        Block[I] = Data[Off + I];
    }
    Sha256Finish(S, Block, N, 64u + Len);
    for (I = 0; I < 32; I++) {
        Inner[I] = Block[I];
    }
    for (I = 0; I < 64; I++) {
        Outer[I] = (UINT8)(Kpad[I] ^ 0x5cu);
    }
    for (I = 0; I < 32; I++) {
        Outer[64 + I] = Inner[I];
    }
    Sha256(Outer, 96, Out);
}

/*
 * 802.11 KDF-SHA-256：counter(2, 从 1) || label || 0x00 || context || bitlen(2)
 */
void IwlKdfSha256(const UINT8 *Key, UINTN KeyLen, const char *Label,
                  const UINT8 *Ctx, UINTN CtxLen, UINT8 *Out, UINTN OutLen)
{
    UINT8 Msg[160];
    UINT8 Hash[32];
    UINTN LabLen = 0;
    UINTN Copied = 0;
    UINT16 Counter = 1;
    UINT16 Bits = (UINT16)(OutLen * 8u);

    if (Label == NULL || Out == NULL || OutLen == 0) {
        return;
    }
    while (Label[LabLen] != 0) {
        LabLen++;
    }
    while (Copied < OutLen) {
        UINTN M = 0;
        UINTN I;
        UINTN Take;
        Msg[M++] = (UINT8)(Counter >> 8);
        Msg[M++] = (UINT8)Counter;
        for (I = 0; I < LabLen && M < sizeof(Msg); I++) {
            Msg[M++] = (UINT8)Label[I];
        }
        if (M < sizeof(Msg)) {
            Msg[M++] = 0;
        }
        for (I = 0; I < CtxLen && M < sizeof(Msg); I++) {
            Msg[M++] = Ctx[I];
        }
        if (M + 2 <= sizeof(Msg)) {
            Msg[M++] = (UINT8)(Bits >> 8);
            Msg[M++] = (UINT8)Bits;
        }
        HmacSha256(Key, KeyLen, Msg, M, Hash);
        Take = OutLen - Copied;
        if (Take > 32) {
            Take = 32;
        }
        for (I = 0; I < Take; I++) {
            Out[Copied + I] = Hash[I];
        }
        Copied += Take;
        Counter++;
    }
}
