/*
 * IwlMvmAssoc.c — MAC assoc / LQ / 固件钥（PR-S-iwl-split-3）
 */
#include "IwlMvmInternal.h"
#include "HalSerial.h"

int IwlMacCtxtPrep(void) {
    UINTN i;

    for (i = 0; i < 6; i++) {
        gIwlBssid[i] = gIwlTarget.Bssid[i];
    }
    if (!IwlMacCtxtSend(IWL_FW_CTXT_ACTION_ADD, 0, 1, 0)) {
        IwlMvmLogCmdFail("macadd", IWL_CMD_MAC_CONTEXT);
        (void)IwlCmdqUnwedge();
        return 0;
    }
    IwlLogStage("macadd=bss0");
    if (!IwlBindingAdd()) {
        return 0;
    }
    IwlProtectSession();
    return 1;
}

/* 刀 #152：站的 16 档速率。数据帧在此之前固件不调度。 */
int IwlSendLq(UINT8 StaId) {
    UINT8 Cmd[88];
    UINT32 Rate6 = IWL_RATE_6M_PLCP | IWL_RATE_MCS_ANT_A;
    UINT32 Rate1 = IWL_RATE_1M_PLCP | IWL_RATE_MCS_CCK | IWL_RATE_MCS_ANT_A;
    UINT32 i;

    IwlMvmZero(Cmd, sizeof(Cmd));
    Cmd[0] = StaId;
    Cmd[6] = (UINT8)IWL_ANT_A;
    Cmd[7] = (UINT8)IWL_ANT_AB;
    IwlMvmPut16(Cmd + 12, 4000);
    Cmd[15] = 1;
    for (i = 0; i < 16u; i++) {
        IwlMvmPut32(Cmd + 20 + i * 4u, (i < 12u) ? Rate6 : Rate1);
    }
    return IwlSendCmd(IWL_CMD_LQ, Cmd, sizeof(Cmd), 400) == 0;
}

/* 刀 #162：AP 站建站时 tid_disable_tx=0xffff。数据改走这站之前先清掉。 */
int IwlStaEnableTx(UINT8 StaId) {
    UINT8 Sta[IWL_ADD_STA_CMD_SIZE];

    IwlMvmZero(Sta, sizeof(Sta));
    Sta[0] = (UINT8)IWL_STA_MODE_MODIFY;
    Sta[16] = StaId;
    Sta[17] = (UINT8)IWL_STA_MODIFY_TID_DISABLE_TX;
    return IwlSendCmd(IWL_CMD_ADD_STA, Sta, sizeof(Sta), 200) == 0;
}

/*
 * 刀 #123/#125/#128：apsta → macmod → apqhw。
 * 刀 #132：mac/bind/TE 已在 auth 后 Prep。
 */
int IwlMacCtxtAssoc(void) {
    int ApOk;

    ApOk = IwlAddApSta();
    if (!ApOk) {
        IwlLogStage("apsta=soft");
    }
    if (IwlMacCtxtSend(IWL_FW_CTXT_ACTION_MODIFY, 1, 200, 0)) {
        IwlLogStage("macmod=ok");
    } else {
        IwlLogStage("macmod=soft");
        (void)IwlCmdqUnwedge();
    }
    if (ApOk) {
        if (!IwlEnableApTxq()) {
            IwlLogStage("apqhw=soft");
            IwlAuxTxLogReset();
        }
        IwlLogStage(IwlSendLq((UINT8)IWL_AP_STA_ID) ? "lq=ok" : "lq=fail");
        IwlLogStage(IwlStaEnableTx((UINT8)IWL_AP_STA_ID) ? "tid=ok" : "tid=fail");
    }
    return 1;
}

/*
 * 刀 #180：ADD_STA_KEY 通（key=ok），但 rx=st=00C07A1F = CCM+DEC_DONE 无 MIC_OK。
 * 刀 #181：OpenBSD 模型——只把 PTK 交给固件；组播保留 DIS_GRP_DECRYPT，主机解 GTK。
 * 固件用错 GTK 会把密文解坏，主机回退也救不了。
 */
int IwlAddStaKey(UINT8 KeyOff, UINT16 Flags, const UINT8 Key[16]) {
    UINT8 Cmd[IWL_ADD_STA_KEY_CMD_V1_SIZE];

    IwlMvmZero(Cmd, sizeof(Cmd));
    Cmd[0] = (UINT8)IWL_AP_STA_ID;
    Cmd[1] = KeyOff;
    IwlMvmPut16(Cmd + 2, Flags);
    IwlMvmCopy(Cmd + 4, Key, 16);
    return IwlSendCmd(IWL_CMD_ADD_STA_KEY, Cmd, sizeof(Cmd), 200) == 0;
}

int IwlStaKeysInstall(void) {
    /* 与 chitti 一致：CCM+KEYID，不置 WEP_KEY_MAP */
    UINT16 PtkFlg = (UINT16)IWL_STA_KEY_FLG_CCM;

    if (!IwlAddStaKey(0, PtkFlg, gIwlPtk)) {
        IwlLogStage("key=ptk");
        return 0;
    }
    /*
     * 刀 #184：保持 DIS_DECRYPT|DIS_GRP（FwDecrypt=0）。
     * 组播是 TKIP；单播 DHCP Offer 由主机用 PTK/CCMP 解，勿让固件先解坏。
     */
    IwlLogStage("key=ok");
    return 1;
}
