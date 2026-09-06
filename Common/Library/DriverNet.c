/*
 * DriverNet.c — Net 类适配层（PR-D3）
 */
#include "DriverNet.h"
#include "Debug.h"

static const NET_BACKEND *gNetBackend;

int ToyDriverNetAttach(const NET_BACKEND *Backend) {
    if (!Backend || !Backend->Ready || !Backend->Poll || !Backend->GetMac ||
        !Backend->GetIp || !Backend->FormatIp || !Backend->ParseIp ||
        !Backend->Ping || !Backend->GetStats || !Backend->SendIp ||
        !Backend->Checksum || !Backend->SetLwIpRx) {
        DebugWrite("drv-net: bad backend\n");
        return -1;
    }
    gNetBackend = Backend;
    return 0;
}

int ToyDriverNetReady(void) {
    if (!gNetBackend || !gNetBackend->Ready) {
        return 0;
    }
    return gNetBackend->Ready();
}

void ToyDriverNetPoll(void) {
    if (gNetBackend && gNetBackend->Poll) {
        gNetBackend->Poll();
    }
}

void ToyDriverNetGetMac(UINT8 Mac[6]) {
    if (gNetBackend && gNetBackend->GetMac) {
        gNetBackend->GetMac(Mac);
    } else if (Mac) {
        Mac[0] = Mac[1] = Mac[2] = Mac[3] = Mac[4] = Mac[5] = 0;
    }
}

UINT32 ToyDriverNetGetIp(void) {
    if (!gNetBackend || !gNetBackend->GetIp) {
        return 0;
    }
    return gNetBackend->GetIp();
}

void ToyDriverNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    if (gNetBackend && gNetBackend->FormatIp) {
        gNetBackend->FormatIp(Ip, Buf, BufLen);
    } else if (Buf && BufLen > 0) {
        Buf[0] = 0;
    }
}

int ToyDriverNetParseIp(const char *Text, UINT32 *Ip) {
    if (!gNetBackend || !gNetBackend->ParseIp) {
        return -1;
    }
    return gNetBackend->ParseIp(Text, Ip);
}

int ToyDriverNetPing(const char *Host, int TimeoutMs) {
    if (!gNetBackend || !gNetBackend->Ping) {
        return -1;
    }
    return gNetBackend->Ping(Host, TimeoutMs);
}

void ToyDriverNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (gNetBackend && gNetBackend->GetStats) {
        gNetBackend->GetStats(TxDone, RxFrames);
        return;
    }
    if (TxDone) {
        *TxDone = 0;
    }
    if (RxFrames) {
        *RxFrames = 0;
    }
}

int ToyDriverNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    if (!gNetBackend || !gNetBackend->SendIp) {
        return -1;
    }
    return gNetBackend->SendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 ToyDriverNetChecksum(const void *Data, UINTN Len) {
    if (!gNetBackend || !gNetBackend->Checksum) {
        return 0;
    }
    return gNetBackend->Checksum(Data, Len);
}

void ToyDriverNetSetLwIpRx(int Enable) {
    if (gNetBackend && gNetBackend->SetLwIpRx) {
        gNetBackend->SetLwIpRx(Enable);
    }
}
