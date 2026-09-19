/*
 * StoreNetParse.c — 响应正文与 FNV-1a-32
 * 核心：StoreNet.c
 */
#include "StoreNetPrivate.h"

static UINT32 Fnv1a32(const UINT8 *Data, UINTN Len) {
    UINT32 H = 2166136261u;
    UINTN i;

    for (i = 0; i < Len; i++) {
        H ^= Data[i];
        H *= 16777619u;
    }
    return H;
}

static void Hex8(UINT32 V, char Out[9]) {
    static const char *D = "0123456789abcdef";
    int i;

    for (i = 0; i < 8; i++) {
        Out[7 - i] = D[(V >> (i * 4)) & 0xF];
    }
    Out[8] = 0;
}

static int HexNibble(char C) {
    if (C >= '0' && C <= '9') {
        return C - '0';
    }
    if (C >= 'a' && C <= 'f') {
        return C - 'a' + 10;
    }
    if (C >= 'A' && C <= 'F') {
        return C - 'A' + 10;
    }
    return -1;
}

int HashOk(const char *Expect, const UINT8 *Data, UINTN Len) {
    char Got[9];
    int i;

    if (!Expect || Expect[0] == 0 || Expect[0] == '-') {
        return 1;
    }
    /* 教学：仅认 8 位 FNV-1a-32 hex */
    for (i = 0; i < 8; i++) {
        if (HexNibble(Expect[i]) < 0) {
            return 1; /* 非 8hex 则跳过 */
        }
    }
    if (Expect[8] != 0 && Expect[8] != ' ' && Expect[8] != '|') {
        return 1;
    }
    Hex8(Fnv1a32(Data, Len), Got);
    for (i = 0; i < 8; i++) {
        char A = Expect[i];
        char B = Got[i];
        if (A >= 'A' && A <= 'F') {
            A = (char)(A - 'A' + 'a');
        }
        if (A != B) {
            return 0;
        }
    }
    return 1;
}

int FindBody(const UINT8 *Resp, UINTN Len, UINTN *BodyOff, UINTN *BodyLen,
                    int *HaveLen) {
    UINTN i;
    UINTN Status = 0;
    UINTN ContentLen = (UINTN)-1;
    UINTN HdrEnd = 0;

    if (HaveLen) {
        *HaveLen = 0;
    }
    if (Len < 12) {
        return -1;
    }
    /* HTTP/1.x 200 */
    if (!(Resp[0] == 'H' && Resp[1] == 'T' && Resp[2] == 'T' && Resp[3] == 'P')) {
        return -1;
    }
    for (i = 0; i + 2 < Len && Resp[i] != ' '; i++) {
    }
    if (i + 3 < Len) {
        Status = (UINTN)(Resp[i + 1] - '0') * 100 +
                 (UINTN)(Resp[i + 2] - '0') * 10 +
                 (UINTN)(Resp[i + 3] - '0');
    }
    if (Status != 200) {
        return -2;
    }
    for (i = 0; i + 1 < Len; i++) {
        if (Resp[i] == '\r' && Resp[i + 1] == '\n' &&
            i + 3 < Len && Resp[i + 2] == '\r' && Resp[i + 3] == '\n') {
            HdrEnd = i + 4;
            break;
        }
        if (Resp[i] == '\n' && Resp[i + 1] == '\n') {
            HdrEnd = i + 2;
            break;
        }
    }
    if (HdrEnd == 0) {
        return -1;
    }
    /* Content-Length（可选，大小写不敏感） */
    for (i = 0; i + 15 < HdrEnd; i++) {
        const char *Key = "content-length:";
        int Match = 1;
        int k;

        for (k = 0; Key[k]; k++) {
            char C = (char)Resp[i + (UINTN)k];
            if (C >= 'A' && C <= 'Z') {
                C = (char)(C - 'A' + 'a');
            }
            if (C != Key[k]) {
                Match = 0;
                break;
            }
        }
        if (Match) {
            UINTN j = i + 15;
            while (j < HdrEnd && (Resp[j] == ' ' || Resp[j] == '\t')) {
                j++;
            }
            ContentLen = 0;
            while (j < HdrEnd && Resp[j] >= '0' && Resp[j] <= '9') {
                ContentLen = ContentLen * 10 + (UINTN)(Resp[j] - '0');
                j++;
            }
            if (HaveLen) {
                *HaveLen = 1;
            }
            break;
        }
    }
    *BodyOff = HdrEnd;
    if (ContentLen != (UINTN)-1) {
        *BodyLen = ContentLen;
    } else {
        *BodyLen = Len > HdrEnd ? Len - HdrEnd : 0;
    }
    return 0;
}
