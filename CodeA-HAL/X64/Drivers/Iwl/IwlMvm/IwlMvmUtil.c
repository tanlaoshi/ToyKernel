/*
 * IwlMvmUtil.c — MVM 小工具（PR-S-iwl-split-3）
 */
#include "IwlMvmInternal.h"
#include "HalSerial.h"

void IwlMvmZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

void IwlMvmCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

void IwlMvmLogCmdFail(const char *Tag, UINT32 Op) {
    char Line[40];
    char Hex[12];
    int n = 0;
    const char *P = Tag;
    while (*P && n < 24) {
        Line[n++] = *P++;
    }
    Line[n++] = ' ';
    Line[n++] = 'o';
    Line[n++] = '=';
    HalSerialFormatHex(Hex, Op, 2);
    Line[n++] = Hex[2];
    Line[n++] = Hex[3];
    Line[n] = 0;
    IwlLogStage(Line);
}

int IwlMvmDqaEnable(void) {
    UINT8 Cmd[4];
    UINT32 Q = IWL_CMD_QUEUE; /* DQA cmd queue = 0 */

    IwlMvmZero(Cmd, sizeof(Cmd));
    Cmd[0] = (UINT8)Q;
    Cmd[1] = (UINT8)(Q >> 8);
    Cmd[2] = (UINT8)(Q >> 16);
    Cmd[3] = (UINT8)(Q >> 24);
    if (IwlSendCmd(IWL_CMD_ID(IWL_DQA_ENABLE_CMD, IWL_DATA_PATH_GROUP, 0),
                   Cmd, sizeof(Cmd), 1) != 0) {
        IwlMvmLogCmdFail("mvm=dqa", IWL_DQA_ENABLE_CMD);
        return 0;
    }
    IwlLogVerb("mvm=dqaok");
    return 1;
}

void IwlMvmPut16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
}

void IwlMvmPut32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

