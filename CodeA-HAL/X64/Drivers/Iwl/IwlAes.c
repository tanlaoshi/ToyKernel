/*
 * IwlAes.c — AES-128 encrypt one 16-byte block (teaching-minimal Rijndael)
 */
#include "IwlPrivate.h"

static const UINT8 gSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const UINT8 gRcon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static UINT8 Xtime(UINT8 X)
{
    return (UINT8)((X << 1) ^ ((X & 0x80u) ? 0x1bu : 0u));
}

static void SubBytes(UINT8 S[16])
{
    UINTN I;
    for (I = 0; I < 16; I++) {
        S[I] = gSbox[S[I]];
    }
}

static void ShiftRows(UINT8 S[16])
{
    UINT8 T;
    T = S[1];  S[1] = S[5];  S[5] = S[9];  S[9] = S[13]; S[13] = T;
    T = S[2];  S[2] = S[10]; S[10] = T;
    T = S[6];  S[6] = S[14]; S[14] = T;
    T = S[15]; S[15] = S[11]; S[11] = S[7]; S[7] = S[3];  S[3] = T;
}

static void MixColumns(UINT8 S[16])
{
    UINTN C;
    for (C = 0; C < 4; C++) {
        UINT8 *Col = &S[C * 4];
        UINT8 A0 = Col[0], A1 = Col[1], A2 = Col[2], A3 = Col[3];
        UINT8 X = (UINT8)(A0 ^ A1 ^ A2 ^ A3);
        Col[0] = (UINT8)(A0 ^ Xtime((UINT8)(A0 ^ A1)) ^ X);
        Col[1] = (UINT8)(A1 ^ Xtime((UINT8)(A1 ^ A2)) ^ X);
        Col[2] = (UINT8)(A2 ^ Xtime((UINT8)(A2 ^ A3)) ^ X);
        Col[3] = (UINT8)(A3 ^ Xtime((UINT8)(A3 ^ A0)) ^ X);
    }
}

static void AddRoundKey(UINT8 S[16], const UINT8 *Rk)
{
    UINTN I;
    for (I = 0; I < 16; I++) {
        S[I] ^= Rk[I];
    }
}

static void KeyExpand(const UINT8 Key[16], UINT8 Rk[176])
{
    UINTN I;
    UINT8 T[4];
    UINTN RconI = 1;

    for (I = 0; I < 16; I++) {
        Rk[I] = Key[I];
    }
    for (I = 16; I < 176; I += 4) {
        T[0] = Rk[I - 4];
        T[1] = Rk[I - 3];
        T[2] = Rk[I - 2];
        T[3] = Rk[I - 1];
        if ((I & 15u) == 0) {
            UINT8 U = T[0];
            T[0] = (UINT8)(gSbox[T[1]] ^ gRcon[RconI++]);
            T[1] = gSbox[T[2]];
            T[2] = gSbox[T[3]];
            T[3] = gSbox[U];
        }
        Rk[I]     = (UINT8)(Rk[I - 16] ^ T[0]);
        Rk[I + 1] = (UINT8)(Rk[I - 15] ^ T[1]);
        Rk[I + 2] = (UINT8)(Rk[I - 14] ^ T[2]);
        Rk[I + 3] = (UINT8)(Rk[I - 13] ^ T[3]);
    }
}

void IwlAesKeyExpand(const UINT8 Key[16], UINT8 Rk[176])
{
    KeyExpand(Key, Rk);
}

void IwlAesAddRoundKey(UINT8 S[16], const UINT8 *Rk)
{
    AddRoundKey(S, Rk);
}

int IwlAesEncrypt(const UINT8 Key[16], const UINT8 In[16], UINT8 Out[16])
{
    UINT8 Rk[176];
    UINT8 S[16];
    UINTN R;
    UINTN I;

    if (Key == NULL || In == NULL || Out == NULL) {
        return 0;
    }
    KeyExpand(Key, Rk);
    for (I = 0; I < 16; I++) {
        S[I] = In[I];
    }
    AddRoundKey(S, Rk);
    for (R = 1; R < 10; R++) {
        SubBytes(S);
        ShiftRows(S);
        MixColumns(S);
        AddRoundKey(S, &Rk[R * 16]);
    }
    SubBytes(S);
    ShiftRows(S);
    AddRoundKey(S, &Rk[160]);
    for (I = 0; I < 16; I++) {
        Out[I] = S[I];
    }
    return 1;
}
/* RFC 4493：128-bit 块左移，最高位为 1 时异或 Rb=0x87 */
static void CmacDbl(UINT8 Block[16])
{
    UINT8 Msb = (UINT8)(Block[0] >> 7);
    UINTN I;

    for (I = 0; I < 15; I++) {
        Block[I] = (UINT8)((Block[I] << 1) | (Block[I + 1] >> 7));
    }
    Block[15] = (UINT8)(Block[15] << 1);
    if (Msb) {
        Block[15] = (UINT8)(Block[15] ^ 0x87u);
    }
}

/* M1 密钥描述符版本 3：EAPOL MIC 用 AES-128-CMAC，标签 16 字节 */
void IwlAesCmac(const UINT8 Key[16], const UINT8 *Msg, UINTN Len, UINT8 Out[16])
{
    UINT8 Zero[16];
    UINT8 L[16];
    UINT8 K1[16];
    UINT8 K2[16];
    UINT8 X[16];
    UINT8 Y[16];
    UINTN Nblks;
    UINTN Off;
    UINTN I;
    int Complete;

    for (I = 0; I < 16; I++) {
        Zero[I] = 0;
        X[I] = 0;
    }
    IwlAesEncrypt(Key, Zero, L);
    for (I = 0; I < 16; I++) {
        K1[I] = L[I];
    }
    CmacDbl(K1);
    for (I = 0; I < 16; I++) {
        K2[I] = K1[I];
    }
    CmacDbl(K2);

    if (Len == 0 || (Len % 16u) != 0) {
        Nblks = Len / 16u + 1u;
        Complete = 0;
    } else {
        Nblks = Len / 16u;
        Complete = 1;
    }
    Off = 0;
    if (Nblks > 1) {
        UINTN B;
        for (B = 0; B < Nblks - 1u; B++) {
            for (I = 0; I < 16; I++) {
                Y[I] = (UINT8)(X[I] ^ Msg[Off + I]);
            }
            IwlAesEncrypt(Key, Y, X);
            Off += 16u;
        }
    }
    for (I = 0; I < 16; I++) {
        Y[I] = 0;
    }
    if (Complete) {
        for (I = 0; I < 16; I++) {
            Y[I] = (UINT8)(Msg[Off + I] ^ K1[I] ^ X[I]);
        }
    } else {
        UINTN Rem = Len - Off;
        for (I = 0; I < Rem; I++) {
            Y[I] = Msg[Off + I];
        }
        Y[Rem] = 0x80u;
        for (I = 0; I < 16; I++) {
            Y[I] = (UINT8)(Y[I] ^ K2[I] ^ X[I]);
        }
    }
    IwlAesEncrypt(Key, Y, Out);
}
