/*
 * IwlScanBeacon.c — beacon 解析（PR-S-iwl-split-4）
 */
#include "IwlScanInternal.h"
#include "HalSerial.h"

int IwlParseBeacon(const UINT8 *Frame, UINTN Len) {
    UINTN Pos;
    UINT8 SsidLen = 0;
    UINT8 Ssid[IWL_SSID_MAX];
    UINT8 Bssid[6];
    UINT16 Caps = 0;
    int HasRsn = 0;
    UINT8 RatesLen = 0;
    UINT8 Rates[8];
    UINT8 ExtRatesLen = 0;
    UINT8 ExtRates[8];
    UINT8 HtLen = 0;
    UINT8 Ht[26];
    UINT8 RsnLen = 0;
    UINT8 Rsn[48];
    UINTN i;

    if (Len < 36) {
        return 0;
    }
    if ((Frame[0] & 0xFC) != 0x80 && (Frame[0] & 0xFC) != 0x50) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        Bssid[i] = Frame[16 + i];
    }
    Caps = (UINT16)Frame[34] | ((UINT16)Frame[35] << 8);
    Pos = 36;
    while (Pos + 2 <= Len) {
        UINT8 Id = Frame[Pos];
        UINT8 El = Frame[Pos + 1];
        if (Pos + 2 + El > Len) {
            break;
        }
        if (Id == 0 && El <= IWL_SSID_MAX) {
            SsidLen = El;
            IwlScanCopyN(Ssid, Frame + Pos + 2, El);
        } else if (Id == 1 && El >= 1) {
            /* Supported Rates：拷进 AssocReq，否则 AP 回 status=18 */
            RatesLen = El > 8 ? 8 : El;
            IwlScanCopyN(Rates, Frame + Pos + 2, RatesLen);
        } else if (Id == 3 && El >= 1) {
            gLastPhyChan = Frame[Pos + 2];
        } else if (Id == 50 && El >= 1) {
            ExtRatesLen = El > 8 ? 8 : El;
            IwlScanCopyN(ExtRates, Frame + Pos + 2, ExtRatesLen);
        } else if (Id == 45 && El >= 2) {
            /* 刀 #140：公司 AP 常要 HT Capabilities */
            HtLen = El > 26 ? 26 : El;
            IwlScanCopyN(Ht, Frame + Pos + 2, HtLen);
        } else if (Id == 48 && El >= 2 && (UINTN)El + 2u <= sizeof(Rsn)) {
            RsnLen = (UINT8)(El + 2);
            IwlScanCopyN(Rsn, Frame + Pos, RsnLen);
            HasRsn = 1;
        }
        Pos += 2 + El;
    }
    if (SsidLen == 0 || !IwlScanStrEq(gIwlSsid, Ssid, SsidLen)) {
        return 0;
    }
    IwlScanCopyN(gIwlTarget.Bssid, Bssid, 6);
    gIwlTarget.SsidLen = SsidLen;
    IwlScanCopyN(gIwlTarget.Ssid, Ssid, SsidLen);
    gIwlTarget.Caps = Caps;
    gIwlTarget.HasRsn = HasRsn;
    gIwlTarget.Chan = gLastPhyChan ? gLastPhyChan : 1;
    gIwlTarget.RatesLen = RatesLen;
    IwlScanCopyN(gIwlTarget.Rates, Rates, RatesLen);
    gIwlTarget.ExtRatesLen = ExtRatesLen;
    IwlScanCopyN(gIwlTarget.ExtRates, ExtRates, ExtRatesLen);
    gIwlTarget.HtLen = HtLen;
    IwlScanCopyN(gIwlTarget.Ht, Ht, HtLen);
    gIwlTarget.RsnLen = RsnLen;
    IwlScanCopyN(gIwlTarget.Rsn, Rsn, RsnLen);
    gIwlSsidOk = 1;
    return 1;
}

/* 失败黄字只用前 8 个可见字符，避免一行撑过串口宽度 */
void IwlScanCopyVis(char *Dst, const UINT8 *Src, UINTN Len) {
    UINTN i;
    UINTN N = Len > 8u ? 8u : Len;

    Dst[0] = 0;
    for (i = 0; i < N; i++) {
        UINT8 Ch = Src[i];
        Dst[i] = (Ch >= 32u && Ch < 127u) ? (char)Ch : '.';
    }
    Dst[N] = 0;
}

void IwlNoteBeacon(const UINT8 *Frame, UINTN Len, char *Heard) {
    UINTN Pos;

    if (Heard[0] != 0 || Len < 36u) {
        return;
    }
    Pos = 36;
    while (Pos + 2u <= Len) {
        UINT8 Id = Frame[Pos];
        UINT8 El = Frame[Pos + 1];
        if (Pos + 2u + El > Len) {
            break;
        }
        if (Id == 0) {
            IwlScanCopyVis(Heard, Frame + Pos + 2, El);
            return;
        }
        Pos += 2u + El;
    }
}

/*
 * 刀 #74：#73 满尾 V1 仍无 ACK/0f。UCODE core33/API36 → OpenBSD 走 V7+ADAPTIVE；
 * 刀 #80：#79 uid 仍 to。UCODE TLV 0x1f=N_SCAN_CHANNELS=52，旧硬编码 40
 * → schedule/preq 错位 96B，FW 静默不 ACK。改 52 + 抬 OutLen 上限。
 */

