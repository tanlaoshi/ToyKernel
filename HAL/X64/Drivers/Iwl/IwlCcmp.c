/*
 * IwlCcmp.c — 802.11 CCMP (AES-CCM, M=8, L=2) teaching-minimal
 *
 * Frame layout (encrypt in / decrypt in):
 *   [802.11 MAC hdr | body | 8 bytes MIC space]
 * HdrLen  = MAC header length (no CCMP hdr).
 * BodyLen = plaintext / ciphertext body length (MIC not included).
 * Encrypt writes MIC at Frame[HdrLen+BodyLen]; does not insert CCMP hdr —
 * caller writes PN/KeyID into the 8-byte CCMP header separately.
 */
#include "IwlPrivate.h"

static void MemClr(UINT8 *P, UINTN N)
{
    UINTN I;
    for (I = 0; I < N; I++) {
        P[I] = 0;
    }
}

static void MemCpy(UINT8 *D, const UINT8 *S, UINTN N)
{
    UINTN I;
    for (I = 0; I < N; I++) {
        D[I] = S[I];
    }
}

static void XorBlk(UINT8 *D, const UINT8 *S, UINTN N)
{
    UINTN I;
    for (I = 0; I < N; I++) {
        D[I] ^= S[I];
    }
}

/* PN is 48-bit in UINT64; store big-endian into 6 bytes (802.11 nonce order) */
static void PnToBe(UINT64 Pn, UINT8 Out[6])
{
    Out[0] = (UINT8)(Pn >> 40);
    Out[1] = (UINT8)(Pn >> 32);
    Out[2] = (UINT8)(Pn >> 24);
    Out[3] = (UINT8)(Pn >> 16);
    Out[4] = (UINT8)(Pn >> 8);
    Out[5] = (UINT8)Pn;
}

/*
 * Build AAD + nonce priority from 802.11 header (IEEE 802.11 CCMP).
 * Returns AAD length; fills NoncePri (TID or 0).
 */
static UINTN BuildAad(const UINT8 *Hdr, UINTN HdrLen, UINT8 Aad[32], UINT8 *NoncePri)
{
    UINT16 Fc;
    UINT16 Seq;
    UINTN Off;
    int Qos;
    int A4;

    if (HdrLen < 24) {
        return 0;
    }
    Fc = (UINT16)Hdr[0] | ((UINT16)Hdr[1] << 8);
    /* Mask FC: Retry/PwrMgt/MoreData=0; Protected=1; Order=0 unless QoS */
    Fc &= (UINT16)~0x3800u;
    Fc |= 0x4000u;
    Qos = ((Fc & 0x008Cu) == 0x0088u); /* Data + QoS subtype bit */
    if (!Qos) {
        Fc &= (UINT16)~0x8000u;
    }
    A4 = ((Fc & 0x0300u) == 0x0300u); /* ToDS|FromDS */

    Aad[0] = (UINT8)Fc;
    Aad[1] = (UINT8)(Fc >> 8);
    MemCpy(Aad + 2, Hdr + 4, 18); /* A1 A2 A3 */
    Seq = (UINT16)Hdr[22] | ((UINT16)Hdr[23] << 8);
    Seq &= 0x000Fu; /* fragment number only */
    Aad[20] = (UINT8)Seq;
    Aad[21] = (UINT8)(Seq >> 8);
    Off = 22;

    *NoncePri = 0;
    if (A4) {
        if (HdrLen < 30) {
            return 0;
        }
        MemCpy(Aad + 22, Hdr + 24, 6);
        Off = 28;
    }
    if (Qos) {
        UINTN QcOff = A4 ? 30u : 24u;
        UINT8 Qc0;
        if (HdrLen < QcOff + 2) {
            return 0;
        }
        Qc0 = Hdr[QcOff];
        Aad[Off] = (UINT8)(Qc0 & 0x0Fu); /* TID */
        Aad[Off + 1] = 0;
        *NoncePri = (UINT8)(Qc0 & 0x0Fu);
        Off += 2;
    }
    return Off;
}

/* B0 / A_i nonce: Priority(1) || A2(6) || PN(6) */
static void FillNonce(UINT8 Nonce[13], UINT8 Pri, const UINT8 *A2, UINT64 Pn)
{
    Nonce[0] = Pri;
    MemCpy(Nonce + 1, A2, 6);
    PnToBe(Pn, Nonce + 7);
}

static void CcmAuthCrypt(const UINT8 Key[16], const UINT8 *Aad, UINTN AadLen,
                         const UINT8 Nonce[13], UINT8 *Body, UINTN BodyLen,
                         UINT8 Mic[8], int Encrypt)
{
    UINT8 B[16];
    UINT8 S[16];
    UINT8 X[16];
    UINT8 A[16];
    UINTN I;
    UINTN Off;
    UINT16 Ctr;

    /* B0: Flags=0x59 (Adata=1, M=8, L=2), Nonce, l(m) */
    MemClr(B, 16);
    B[0] = 0x59u;
    MemCpy(B + 1, Nonce, 13);
    B[14] = (UINT8)(BodyLen >> 8);
    B[15] = (UINT8)BodyLen;
    IwlAesEncrypt(Key, B, X);

    /* Auth AAD (encoded length + data, zero-padded to 16) */
    MemClr(B, 16);
    B[0] = (UINT8)(AadLen >> 8);
    B[1] = (UINT8)AadLen;
    Off = 2;
    for (I = 0; I < AadLen; I++) {
        B[Off++] = Aad[I];
        if (Off == 16) {
            XorBlk(X, B, 16);
            IwlAesEncrypt(Key, X, X);
            MemClr(B, 16);
            Off = 0;
        }
    }
    if (Off) {
        XorBlk(X, B, 16);
        IwlAesEncrypt(Key, X, X);
    }

    /* CTR prep: Flags=(L-1)=1, Nonce, counter */
    MemClr(A, 16);
    A[0] = 0x01u;
    MemCpy(A + 1, Nonce, 13);

    /* Auth + encrypt/decrypt body in 16-byte chunks; CTR starts at 1 */
    Ctr = 1;
    Off = 0;
    while (Off < BodyLen) {
        UINTN N = BodyLen - Off;
        if (N > 16) {
            N = 16;
        }
        if (Encrypt) {
            MemClr(B, 16);
            MemCpy(B, Body + Off, N);
            XorBlk(X, B, 16);
            IwlAesEncrypt(Key, X, X);
            A[14] = (UINT8)(Ctr >> 8);
            A[15] = (UINT8)Ctr;
            IwlAesEncrypt(Key, A, S);
            for (I = 0; I < N; I++) {
                Body[Off + I] = (UINT8)(Body[Off + I] ^ S[I]);
            }
        } else {
            A[14] = (UINT8)(Ctr >> 8);
            A[15] = (UINT8)Ctr;
            IwlAesEncrypt(Key, A, S);
            for (I = 0; I < N; I++) {
                Body[Off + I] = (UINT8)(Body[Off + I] ^ S[I]);
            }
            MemClr(B, 16);
            MemCpy(B, Body + Off, N);
            XorBlk(X, B, 16);
            IwlAesEncrypt(Key, X, X);
        }
        Off += N;
        Ctr++;
    }

    /* S0 masks the MIC tag */
    A[14] = 0;
    A[15] = 0;
    IwlAesEncrypt(Key, A, S);
    for (I = 0; I < 8; I++) {
        Mic[I] = (UINT8)(X[I] ^ S[I]);
    }
}

int IwlCcmpEncrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame,
                   UINTN HdrLen, UINTN BodyLen)
{
    UINT8 Aad[32];
    UINT8 Nonce[13];
    UINT8 Mic[8];
    UINT8 Pri;
    UINTN AadLen;
    UINTN I;

    if (Key == NULL || Frame == NULL || HdrLen < 24) {
        return 0;
    }
    AadLen = BuildAad(Frame, HdrLen, Aad, &Pri);
    if (AadLen == 0) {
        return 0;
    }
    FillNonce(Nonce, Pri, Frame + 10, Pn); /* A2 at offset 10 */
    CcmAuthCrypt(Key, Aad, AadLen, Nonce, Frame + HdrLen, BodyLen, Mic, 1);
    for (I = 0; I < 8; I++) {
        Frame[HdrLen + BodyLen + I] = Mic[I];
    }
    return 1;
}

int IwlCcmpDecrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame,
                   UINTN HdrLen, UINTN BodyLen)
{
    UINT8 Aad[32];
    UINT8 Nonce[13];
    UINT8 Mic[8];
    UINT8 Got[8];
    UINT8 Pri;
    UINTN AadLen;
    UINTN I;
    UINT8 Diff;

    if (Key == NULL || Frame == NULL || HdrLen < 24) {
        return 0;
    }
    AadLen = BuildAad(Frame, HdrLen, Aad, &Pri);
    if (AadLen == 0) {
        return 0;
    }
    for (I = 0; I < 8; I++) {
        Got[I] = Frame[HdrLen + BodyLen + I];
    }
    FillNonce(Nonce, Pri, Frame + 10, Pn);
    CcmAuthCrypt(Key, Aad, AadLen, Nonce, Frame + HdrLen, BodyLen, Mic, 0);
    Diff = 0;
    for (I = 0; I < 8; I++) {
        Diff |= (UINT8)(Mic[I] ^ Got[I]);
    }
    return Diff == 0 ? 1 : 0;
}
