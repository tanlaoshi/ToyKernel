/*
 * UITextWrap.c — UTF-8 按像素宽软换行绘制 / 单行截断
 */
#include "UI.h"
#include "HalVideo.h"
#include "Font.h"

void UiFitTextUtf8(char *S, UINT32 MaxW) {
    UINTN N;

    if (!S) {
        return;
    }
    if (MaxW < 8u) {
        S[0] = 0;
        return;
    }
    while (S[0] && FontStringWidth(S) > MaxW) {
        N = 0;
        while (S[N]) {
            N++;
        }
        if (N == 0) {
            break;
        }
        N--;
        while (N > 0 && (((UINT8)S[N]) & 0xC0u) == 0x80u) {
            N--;
        }
        S[N] = 0;
    }
}

UINT32 UiDrawTextWrap(UINT32 X, UINT32 Y, UINT32 MaxW, UINT32 MaxY,
                      UINT32 LineStep, const char *S, UINT32 Color) {
    char Line[128];
    const char *P;
    UINTN i;

    if (!S || MaxW < 4u || LineStep == 0) {
        return Y;
    }
    P = S;
    while (*P) {
        UINTN LineBytes = 0;
        UINT32 LineW = 0;
        const char *Q;

        if (MaxY != 0 && Y + FontCellH() > MaxY) {
            break;
        }
        if (*P == '\n') {
            P++;
            Y += LineStep;
            continue;
        }
        Q = P;
        while (*Q && *Q != '\n') {
            UINT32 Cp;
            UINTN N;
            UINT32 Adv;

            N = Utf8Decode(Q, &Cp);
            if (N == 0) {
                Q++;
                continue;
            }
            Adv = FontCodepointAdvance(Cp);
            if (Adv == 0) {
                Adv = FontAdvanceX();
            }
            if (LineBytes > 0 && LineW + Adv > MaxW) {
                break;
            }
            if (LineBytes + N >= sizeof(Line)) {
                break;
            }
            LineW += Adv;
            LineBytes += N;
            Q += N;
            if (LineBytes > 0 && LineW > MaxW) {
                /* 单字宽于栏：仍画一字，避免死循环 */
                break;
            }
        }
        if (LineBytes == 0) {
            if (*Q == '\n') {
                P = Q + 1;
                Y += LineStep;
                continue;
            }
            break;
        }
        for (i = 0; i < LineBytes; i++) {
            Line[i] = P[i];
        }
        Line[LineBytes] = 0;
        HalVideoDrawStringAt(X, Y, Line, Color);
        Y += LineStep;
        P += LineBytes;
        if (*P == '\n') {
            P++;
        }
    }
    return Y;
}
