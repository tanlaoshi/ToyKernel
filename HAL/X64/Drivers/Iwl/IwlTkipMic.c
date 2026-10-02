/* IwlTkipMic.c — Michael MIC（PR-N-wifi-tkip） */
#include "IwlPrivate.h"

static UINT32 Rotl32(UINT32 V, int N) {
    return (V << N) | (V >> (32 - N));
}

static UINT32 Rotr32(UINT32 V, int N) {
    return (V >> N) | (V << (32 - N));
}

static UINT32 Xswap(UINT32 V) {
    return ((V & 0xFF00FF00u) >> 8) | ((V & 0x00FF00FFu) << 8);
}

static UINT32 GetLe32(const UINT8 *P) {
    return (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16)
         | ((UINT32)P[3] << 24);
}

static void PutLe32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

static void MichaelBlock(UINT32 *L, UINT32 *R) {
    *R ^= Rotl32(*L, 17);
    *L += *R;
    *R ^= Xswap(*L);
    *L += *R;
    *R ^= Rotl32(*L, 3);
    *L += *R;
    *R ^= Rotr32(*L, 2);
    *L += *R;
}

static void MichaelMic(const UINT8 Key[8], const UINT8 Hdr[16],
                       const UINT8 *Data, UINTN DataLen, UINT8 Out[8]) {
    UINT32 L = GetLe32(Key);
    UINT32 R = GetLe32(Key + 4);
    UINT8 Last[4];
    UINTN Off;
    UINTN N;

    for (Off = 0; Off < 16u; Off += 4u) {
        L ^= GetLe32(Hdr + Off);
        MichaelBlock(&L, &R);
    }
    for (Off = 0; Off + 4u <= DataLen; Off += 4u) {
        L ^= GetLe32(Data + Off);
        MichaelBlock(&L, &R);
    }
    N = DataLen - Off;
    Last[0] = Last[1] = Last[2] = Last[3] = 0;
    if (N > 0) {
        Last[0] = Data[Off];
    }
    if (N > 1) {
        Last[1] = Data[Off + 1];
    }
    if (N > 2) {
        Last[2] = Data[Off + 2];
    }
    if (N > 3) {
        Last[3] = Data[Off + 3];
    }
    Last[N] = 0x5a; /* padding start; N<=3 */
    L ^= GetLe32(Last);
    MichaelBlock(&L, &R);
    MichaelBlock(&L, &R);
    PutLe32(Out, L);
    PutLe32(Out + 4, R);
}

static void BuildMichaelHdr(const UINT8 *Mac, UINTN HdrLen, UINT8 Out[16]) {
    UINT16 Fc = (UINT16)Mac[0] | ((UINT16)Mac[1] << 8);
    int Qos = ((Fc & 0x008Cu) == 0x0088u);
    UINT8 Pri = 0;
    UINTN I;

    /* FromDS 组播：DA=Addr1 SA=Addr3 */
    for (I = 0; I < 6u; I++) {
        Out[I] = Mac[4 + I];
        Out[6 + I] = Mac[16 + I];
    }
    if (Qos && HdrLen >= 26u) {
        Pri = (UINT8)(Mac[24] & 0x0Fu);
    }
    Out[12] = Pri;
    Out[13] = 0;
    Out[14] = 0;
    Out[15] = 0;
}

/*
 * Frame=[MAC HdrLen][IV8][MSDU|MIC8|ICV4]；CryptLen=IV 后密文总长（含 MIC+ICV）。
 * 成功后密文区前 CryptLen-12 字节为明文 MSDU；失败则 RC4 再跑还原。
 */

int IwlTkipMichaelOk(const UINT8 Tk[32], const UINT8 *Frame, UINTN HdrLen,
                     const UINT8 *Payload, UINTN DataLen) {
    UINT8 Hdr[16];
    UINT8 Mic[8];
    UINTN I;
    UINT8 Diff = 0;

    BuildMichaelHdr(Frame, HdrLen, Hdr);
    MichaelMic(Tk + 16, Hdr, Payload, DataLen, Mic);
    for (I = 0; I < 8u; I++) {
        Diff |= (UINT8)(Mic[I] ^ Payload[DataLen + I]);
    }
    return Diff == 0;
}
