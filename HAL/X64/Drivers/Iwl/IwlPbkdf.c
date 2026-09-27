/*
 * IwlPbkdf.c — PBKDF2-HMAC-SHA1 for WPA-PSK (teaching-minimal)
 *
 * WPA2-PSK: IwlPbkdf2Sha1(passphrase, ssid, ssidlen, 4096, pmk, 32)
 */
#include "IwlPrivate.h"

static void MemXor(UINT8 *Dst, const UINT8 *Src, UINTN Len)
{
    UINTN I;
    for (I = 0; I < Len; I++) {
        Dst[I] ^= Src[I];
    }
}

static void MemCpy(UINT8 *Dst, const UINT8 *Src, UINTN Len)
{
    UINTN I;
    for (I = 0; I < Len; I++) {
        Dst[I] = Src[I];
    }
}

static UINTN PassLen(const char *Pass)
{
    UINTN N = 0;
    if (Pass == NULL) {
        return 0;
    }
    while (Pass[N] != 0) {
        N++;
    }
    return N;
}

/*
 * F(P, S, c, i) = U1 ^ U2 ^ ... ^ Uc
 * U1 = HMAC(P, S || INT(i)); U_{n+1} = HMAC(P, U_n)
 */
static void Pbkdf2F(const UINT8 *Pass, UINTN Plen,
                    const UINT8 *Salt, UINTN SaltLen,
                    UINT32 Iter, UINT32 BlockIdx, UINT8 Out[20])
{
    UINT8 Msg[64 + 4]; /* salt typically <= 32 (SSID); keep modest */
    UINT8 U[20];
    UINT8 T[20];
    UINTN Mlen;
    UINTN I;
    UINT32 C;

    if (SaltLen > 64) {
        SaltLen = 64;
    }
    MemCpy(Msg, Salt, SaltLen);
    Msg[SaltLen]     = (UINT8)(BlockIdx >> 24);
    Msg[SaltLen + 1] = (UINT8)(BlockIdx >> 16);
    Msg[SaltLen + 2] = (UINT8)(BlockIdx >> 8);
    Msg[SaltLen + 3] = (UINT8)BlockIdx;
    Mlen = SaltLen + 4;

    IwlHmacSha1(Pass, Plen, Msg, Mlen, U);
    MemCpy(T, U, 20);

    for (C = 1; C < Iter; C++) {
        IwlHmacSha1(Pass, Plen, U, 20, U);
        MemXor(T, U, 20);
    }
    for (I = 0; I < 20; I++) {
        Out[I] = T[I];
    }
}

int IwlPbkdf2Sha1(const char *Pass, const UINT8 *Salt, UINTN SaltLen,
                  UINT32 Iter, UINT8 *Out, UINTN OutLen)
{
    UINT8 Block[20];
    UINTN Plen;
    UINTN Copied;
    UINT32 Idx;

    if (Pass == NULL || Salt == NULL || Out == NULL || Iter == 0 || OutLen == 0) {
        return 0;
    }
    Plen = PassLen(Pass);
    if (Plen == 0) {
        return 0;
    }

    Copied = 0;
    Idx = 1;
    while (Copied < OutLen) {
        UINTN Take;
        UINTN I;
        Pbkdf2F((const UINT8 *)Pass, Plen, Salt, SaltLen, Iter, Idx, Block);
        Take = OutLen - Copied;
        if (Take > 20) {
            Take = 20;
        }
        for (I = 0; I < Take; I++) {
            Out[Copied + I] = Block[I];
        }
        Copied += Take;
        Idx++;
    }
    return 1;
}
