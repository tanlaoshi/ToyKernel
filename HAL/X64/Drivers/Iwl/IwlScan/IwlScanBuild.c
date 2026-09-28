/*
 * IwlScanBuild.c — UMAC SCAN_REQ（PR-S-iwl-split-4）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

int IwlBuildUmacScan(UINT8 *Req, UINT32 *OutLen) {
    UINT8 *ChanData;
    UINT8 *Tail;
    UINT8 *Preq;
    UINT8 *Direct;
    UINT8 *Frm;
    UINT32 Gen;
    UINT32 i;
    static const UINT8 Chans[] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
    };
    UINT32 Nact = 13;

    IwlScanZero(Req, IWL_SCAN_REQ_MAX);
    IwlScanPut32(Req + 0, 0);
    /*
     * 刀 #79：#78 BC 仍 cmdto。旧 uid=1 → Linux UID_TYPE=SCHED；
     * OpenBSD 置 0（REGULAR）。改回 0。
     */
    IwlScanPut32(Req + 4, 0);
    /* 刀 #77：FW 有 EXT_SCAN_PRIORITY；Linux 用 EXT_6，旧 HIGH=2 可能被忽略 */
    IwlScanPut32(Req + 8, IWL_SCAN_PRIORITY_EXT_6);
    Gen = IWL_UMAC_SCAN_GEN_PASS_ALL | IWL_UMAC_SCAN_GEN_ITER_COMPLETE
        | IWL_UMAC_SCAN_GEN_ADAPTIVE_DWELL | IWL_UMAC_SCAN_GEN_PASSIVE;
    IwlScanPut16(Req + 12, (UINT16)Gen);
    Req[14] = 0;
    Req[15] = 0;
    /* v7 dwell / adwell（OpenBSD iwm_umac_scan） */
    Req[16] = 10;
    Req[17] = 110;
    Req[18] = 44;
    Req[19] = (UINT8)IWL_SCAN_ADWELL_N_APS;
    Req[20] = (UINT8)IWL_SCAN_ADWELL_N_APS_SOCIAL;
    Req[21] = 0;
    IwlScanPut16(Req + 22, (UINT16)IWL_SCAN_ADWELL_BUDGET_FULL);
    IwlScanPut32(Req + 24, 0);
    IwlScanPut32(Req + 28, 0);
    IwlScanPut32(Req + 32, 0);
    IwlScanPut32(Req + 36, 0);
    IwlScanPut32(Req + 40, IWL_SCAN_PRIORITY_EXT_6);
    Req[44] = 0;
    Req[45] = (UINT8)Nact;
    Req[46] = 0;
    Req[47] = 0;

    ChanData = Req + IWL_SCAN_REQ_UMAC_SIZE_V7;
    for (i = 0; i < IWL_SCAN_NCHAN_CAPA; i++) {
        UINT8 *C = ChanData + i * 8;
        if (i < Nact) {
            IwlScanPut32(C, 0);
            C[4] = Chans[i];
            C[5] = 1;
            IwlScanPut16(C + 6, 0);
        }
    }

    Tail = ChanData + IWL_SCAN_NCHAN_CAPA * 8;
    IwlScanPut16(Tail + 0, 0);
    Tail[2] = 1;
    Tail[3] = 0;
    IwlScanPut16(Tail + 8, 0);
    IwlScanPut16(Tail + 10, 0);

    Preq = Tail + 12;
    Frm = Preq + 16;
    Frm[0] = 0x40;
    Frm[1] = 0x00;
    Frm[2] = 0;
    Frm[3] = 0;
    for (i = 0; i < 6; i++) {
        Frm[4 + i] = 0xff;
    }
    IwlScanCopyN(Frm + 10, gIwlMac, 6);
    for (i = 0; i < 6; i++) {
        Frm[16 + i] = 0xff;
    }
    Frm[22] = 0;
    Frm[23] = 0;
    Frm[24] = 0;
    Frm[25] = 0;
    Frm[26] = 1;
    Frm[27] = 8;
    Frm[28] = 0x82;
    Frm[29] = 0x84;
    Frm[30] = 0x8b;
    Frm[31] = 0x96;
    Frm[32] = 0x0c;
    Frm[33] = 0x12;
    Frm[34] = 0x18;
    Frm[35] = 0x24;
    Frm[36] = 50;
    Frm[37] = 4;
    Frm[38] = 0x30;
    Frm[39] = 0x48;
    Frm[40] = 0x60;
    Frm[41] = 0x6c;
    IwlScanPut16(Preq + 0, 0);
    IwlScanPut16(Preq + 2, 26);
    IwlScanPut16(Preq + 4, 26);
    IwlScanPut16(Preq + 6, 16);
    IwlScanPut16(Preq + 8, 0);
    IwlScanPut16(Preq + 10, 0);
    IwlScanPut16(Preq + 12, 0);
    IwlScanPut16(Preq + 14, 0);

    Direct = Preq + 16 + IWL_SCAN_PROBE_REQ_SIZE;
    *OutLen = (UINT32)(Direct + IWL_PROBE_OPTION_MAX * IWL_SSID_IE_SIZE - Req);
    return 1;
}

