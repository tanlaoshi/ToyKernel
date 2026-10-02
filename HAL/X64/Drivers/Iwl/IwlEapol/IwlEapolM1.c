/*
 * IwlEapolM1.c — 等 EAPOL msg1 编排（PR-F-iwl-1）
 * Start/Poll/Diag 见同目录 IwlEapolM1{Start,Poll,Diag}.c
 */
#include "IwlEapolInternal.h"

int IwlEapolWaitMsg1(UINT8 *Eapol, UINTN *EapLen, UINT8 *Anonce, UINT8 *Replay,
                     UINT8 *KeyDescOut) {
    IWL_EAPOL_M1_CTX Ctx;
    UINT32 I;

    IwlEapolM1CtxInit(&Ctx);
    IwlEapolM1SendStart(&Ctx);

    for (I = 0; I < 4000; I++) {
        if (Ctx.Got1 && I >= Ctx.M1At + 100u) {
            break;
        }
        if ((I % 500u) == 499u) {
            (void)IwlSendEapol(Ctx.Start, 4);
        }

        IwlRxPoll();
        if (IwlEapolM1DrainRx(&Ctx, Eapol, EapLen, Anonce, Replay, I)) {
            break;
        }
        if (!Ctx.Got1 || I < Ctx.M1At + 100u) {
            IwlStallMs(1);
        }
    }
    if (!Ctx.Got1) {
        IwlEapolM1LogTimeout(&Ctx);
        return 0;
    }

    *KeyDescOut = Ctx.KeyDesc;
    return Ctx.Got1;
}
