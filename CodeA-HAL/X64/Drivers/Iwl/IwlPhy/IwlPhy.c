/*
 * IwlPhy.c — PHY 状态 / 下发 CFG+ANT（PR-S-iwl-split-5）
 */
#include "IwlPhyInternal.h"
#include "HalSerial.h"

UINT8 gPhyCfgData[IWL_PHY_CFG_MAX];
UINT16 gPhyCfgLen;
UINT8 gPhyNchData[IWL_PHY_NCH_MAX];
UINT16 gPhyNchLen;
UINT8 gPapdData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
UINT16 gPapdLen[IWL_NUM_CH_GROUPS];
UINT8 gTxpData[IWL_NUM_CH_GROUPS][IWL_PHY_CHG_MAX];
UINT16 gTxpLen[IWL_NUM_CH_GROUPS];
UINT32 gPhyNotifN;
UINT32 gPapdN;
UINT32 gTxpN;
UINT32 gPhyDropN; /* 超长丢弃计数 */

void IwlPhyZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

void IwlPhyCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

void IwlPhyPut32(UINT8 *P, UINT32 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
    P[2] = (UINT8)(V >> 16);
    P[3] = (UINT8)(V >> 24);
}

void IwlPhyPut16(UINT8 *P, UINT16 V) {
    P[0] = (UINT8)V;
    P[1] = (UINT8)(V >> 8);
}

int IwlPhySendCfg(UINT32 UcodeType) {
    UINT8 Cmd[12];
    UINT32 Flow = gIwlCalibFlow[UcodeType];
    UINT32 Event = gIwlCalibEvent[UcodeType];

    IwlPhyZero(Cmd, sizeof(Cmd));
    IwlPhyPut32(Cmd, gIwlPhyCfg);
    IwlPhyPut32(Cmd + 4, Flow);
    IwlPhyPut32(Cmd + 8, Event);
    if (IwlSendCmd(IWL_CMD_PHY_CONFIG, Cmd, sizeof(Cmd), 1) != 0) {
        return 0;
    }
    return 1;
}

int IwlPhySendAnt(void) {
    UINT8 Cmd[4];
    UINT32 Ant = IWL_ANT_AB;

    IwlPhyZero(Cmd, sizeof(Cmd));
    IwlPhyPut32(Cmd, Ant);
    return IwlSendCmd(IWL_CMD_TX_ANT_CFG, Cmd, sizeof(Cmd), 1) == 0;
}


int IwlPhyCfgRt(void) {
    if (!IwlPhySendCfg(IWL_UCODE_TYPE_REGULAR)) {
        IwlLogStage("phyrt=fail");
        return 0;
    }
    IwlLogVerb("phyrt=ok");
    return 1;
}
