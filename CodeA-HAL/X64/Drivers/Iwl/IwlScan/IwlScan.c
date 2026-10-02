/*
 * IwlScan.c — 扫描入口（PR-S-iwl-split-4）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

IWL_BSS gIwlTarget;
int gIwlScanCount;
int gIwlSsidOk;

int IwlScanRun(void) {
    static UINT8 Req[IWL_SCAN_REQ_MAX];
    UINT32 ReqLen = 0;

    gIwlScanCount = 0;
    gIwlSsidOk = 0;
    gLastPhyChan = 0;
    IwlLogVerb("mvm=v83");

    if (!IwlBuildUmacScan(Req, &ReqLen) || ReqLen > IWL_CMD_PAYLOAD_MAX) {
        IwlLogStage("scan=build");
        return 0;
    }
    {
        char Line[24];
        char Hex[12];
        int n = 0;
        const char *P = "scan=L";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, ReqLen, 4);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = Hex[4];
        Line[n++] = Hex[5];
        Line[n] = 0;
        IwlLogVerb(Line);
    }
    /*
     * 刀 #77：#76 换序仍静默。改 sync（OpenBSD 前台扫）+ EXT_6；
     * 刀 #80：n_scan_channels=52（对齐 TLV）。
     */
    if (IwlSendCmd(IWL_CMD_ID(IWL_CMD_SCAN_REQ_UMAC, IWL_LONG_GROUP, 0),
                   Req, ReqLen, 1) != 0) {
        IwlLogStage("scan=to");
        IwlCmdqUnwedge();
        return 0;
    }
    IwlLogVerb("scan=ack");
    IwlCmdqSnap();

    return IwlScanCollect();
}
