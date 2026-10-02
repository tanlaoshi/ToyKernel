/* IwlTkip.c — TKIP 组播 RX（PR-N-wifi-tkip）；对齐 IEEE / lib80211_crypt_tkip */
#include "IwlPrivate.h"

static const UINT16 gSbox[256] = {
    0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
    0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
    0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
    0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
    0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
    0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
    0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
    0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
    0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
    0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
    0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
    0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
    0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
    0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
    0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
    0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
    0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
    0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
    0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
    0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
    0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
    0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
    0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
    0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
    0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
    0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
    0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
    0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
    0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
    0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
    0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
    0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A,
};

static UINT16 RotR1(UINT16 V) {
    return (UINT16)(((V >> 1) & 0x7FFFu) | ((V & 1u) << 15));
}

static UINT16 Mk16(UINT8 Hi, UINT8 Lo) {
    return (UINT16)(((UINT16)Hi << 8) | Lo);
}

static UINT16 Mk16Le(const UINT8 *P) {
    return (UINT16)((UINT16)P[0] | ((UINT16)P[1] << 8));
}

static UINT16 Lo8(UINT16 V) {
    return (UINT16)(V & 0xFFu);
}

static UINT16 Hi8(UINT16 V) {
    return (UINT16)((V >> 8) & 0xFFu);
}

static UINT16 SBox(UINT16 V) {
    UINT16 T = gSbox[Hi8(V)];
    return (UINT16)(gSbox[Lo8(V)] ^ (UINT16)((T << 8) | (T >> 8)));
}

static void Phase1(UINT16 *Ttak, const UINT8 *Tk, const UINT8 *Ta, UINT32 Iv32) {
    int I;
    int J;

    Ttak[0] = (UINT16)(Iv32 & 0xFFFFu);
    Ttak[1] = (UINT16)((Iv32 >> 16) & 0xFFFFu);
    Ttak[2] = Mk16(Ta[1], Ta[0]);
    Ttak[3] = Mk16(Ta[3], Ta[2]);
    Ttak[4] = Mk16(Ta[5], Ta[4]);
    for (I = 0; I < 8; I++) {
        J = 2 * (I & 1);
        Ttak[0] = (UINT16)(Ttak[0] + SBox((UINT16)(Ttak[4] ^ Mk16(Tk[1 + J], Tk[0 + J]))));
        Ttak[1] = (UINT16)(Ttak[1] + SBox((UINT16)(Ttak[0] ^ Mk16(Tk[5 + J], Tk[4 + J]))));
        Ttak[2] = (UINT16)(Ttak[2] + SBox((UINT16)(Ttak[1] ^ Mk16(Tk[9 + J], Tk[8 + J]))));
        Ttak[3] = (UINT16)(Ttak[3] + SBox((UINT16)(Ttak[2] ^ Mk16(Tk[13 + J], Tk[12 + J]))));
        Ttak[4] = (UINT16)(Ttak[4] + SBox((UINT16)(Ttak[3] ^ Mk16(Tk[1 + J], Tk[0 + J]))) + (UINT16)I);
    }
}

static void Phase2(UINT8 *WepSeed, const UINT8 *Tk, const UINT16 *Ttak, UINT16 Iv16) {
    UINT16 Ppk[6];
    int I;

    for (I = 0; I < 5; I++) {
        Ppk[I] = Ttak[I];
    }
    Ppk[5] = (UINT16)(Ttak[4] + Iv16);
    Ppk[0] = (UINT16)(Ppk[0] + SBox((UINT16)(Ppk[5] ^ Mk16Le(Tk + 0))));
    Ppk[1] = (UINT16)(Ppk[1] + SBox((UINT16)(Ppk[0] ^ Mk16Le(Tk + 2))));
    Ppk[2] = (UINT16)(Ppk[2] + SBox((UINT16)(Ppk[1] ^ Mk16Le(Tk + 4))));
    Ppk[3] = (UINT16)(Ppk[3] + SBox((UINT16)(Ppk[2] ^ Mk16Le(Tk + 6))));
    Ppk[4] = (UINT16)(Ppk[4] + SBox((UINT16)(Ppk[3] ^ Mk16Le(Tk + 8))));
    Ppk[5] = (UINT16)(Ppk[5] + SBox((UINT16)(Ppk[4] ^ Mk16Le(Tk + 10))));
    Ppk[0] = (UINT16)(Ppk[0] + RotR1((UINT16)(Ppk[5] ^ Mk16Le(Tk + 12))));
    Ppk[1] = (UINT16)(Ppk[1] + RotR1((UINT16)(Ppk[0] ^ Mk16Le(Tk + 14))));
    Ppk[2] = (UINT16)(Ppk[2] + RotR1(Ppk[1]));
    Ppk[3] = (UINT16)(Ppk[3] + RotR1(Ppk[2]));
    Ppk[4] = (UINT16)(Ppk[4] + RotR1(Ppk[3]));
    Ppk[5] = (UINT16)(Ppk[5] + RotR1(Ppk[4]));

    WepSeed[0] = (UINT8)Hi8(Iv16);
    WepSeed[1] = (UINT8)((Hi8(Iv16) | 0x20u) & 0x7Fu);
    WepSeed[2] = (UINT8)Lo8(Iv16);
    WepSeed[3] = (UINT8)Lo8((UINT16)((Ppk[5] ^ Mk16Le(Tk + 0)) >> 1));
    for (I = 0; I < 6; I++) {
        WepSeed[4 + 2 * I] = (UINT8)Lo8(Ppk[I]);
        WepSeed[5 + 2 * I] = (UINT8)Hi8(Ppk[I]);
    }
}

static void Rc4Crypt(const UINT8 Key[16], UINT8 *Data, UINTN Len) {
    UINT8 S[256];
    UINTN I;
    UINTN J;
    UINTN K;
    UINT8 T;

    for (I = 0; I < 256u; I++) {
        S[I] = (UINT8)I;
    }
    J = 0;
    for (I = 0; I < 256u; I++) {
        J = (J + S[I] + Key[I & 15u]) & 255u;
        T = S[I];
        S[I] = S[J];
        S[J] = T;
    }
    I = 0;
    J = 0;
    for (K = 0; K < Len; K++) {
        I = (I + 1u) & 255u;
        J = (J + S[I]) & 255u;
        T = S[I];
        S[I] = S[J];
        S[J] = T;
        Data[K] ^= S[(S[I] + S[J]) & 255u];
    }
}

static UINT32 Crc32(const UINT8 *Data, UINTN Len) {
    UINT32 C = 0xFFFFFFFFu;
    UINTN I;
    int B;

    for (I = 0; I < Len; I++) {
        C ^= Data[I];
        for (B = 0; B < 8; B++) {
            C = (C >> 1) ^ (0xEDB88320u & (UINT32)(-(INT32)(C & 1u)));
        }
    }
    return ~C;
}

int IwlTkipDecrypt(const UINT8 Tk[32], UINT8 *Frame, UINTN HdrLen, UINTN CryptLen) {
    UINT8 *Iv;
    UINT8 *Payload;
    UINT8 Rc4Key[16];
    UINT16 Ttak[5];
    UINT16 Iv16;
    UINT32 Iv32;
    UINT32 Icv;
    UINT32 Got;
    UINTN DataLen;

    if (Tk == 0 || Frame == 0 || HdrLen < 24u || CryptLen < 12u) {
        return 0;
    }
    Iv = Frame + HdrLen;
    Payload = Iv + 8;
    Iv16 = (UINT16)(((UINT16)Iv[0] << 8) | Iv[2]);
    Iv32 = (UINT32)Iv[4] | ((UINT32)Iv[5] << 8) | ((UINT32)Iv[6] << 16)
         | ((UINT32)Iv[7] << 24);
    Phase1(Ttak, Tk, Frame + 10, Iv32);
    Phase2(Rc4Key, Tk, Ttak, Iv16);
    Rc4Crypt(Rc4Key, Payload, CryptLen);

    DataLen = CryptLen - 12u;
    Got = (UINT32)Payload[DataLen + 8] | ((UINT32)Payload[DataLen + 9] << 8)
        | ((UINT32)Payload[DataLen + 10] << 16)
        | ((UINT32)Payload[DataLen + 11] << 24);
    Icv = Crc32(Payload, DataLen + 8u);
    if (Icv != Got) {
        Rc4Crypt(Rc4Key, Payload, CryptLen);
        return 0;
    }
    if (!IwlTkipMichaelOk(Tk, Frame, HdrLen, Payload, DataLen)) {
        Rc4Crypt(Rc4Key, Payload, CryptLen);
        return 0;
    }
    return 1;
}
