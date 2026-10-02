/*
 * HalDevicesNet.c — Arm64：Net 门面（PR-S3-haldev-1）
 */
#include "Hal.h"
#include "DriverNet.h"
#include "VirtioNet.h"

int HalNetInit(void) {
    return VirtioNetInit();
}

int HalNetReady(void) {
    return ToyDriverNetReady();
}

UINT32 HalNetNicEpoch(void) {
    return 0;
}

void HalNetPoll(void) {
    ToyDriverNetPoll();
}

void HalNetGetMacAddress(UINT8 Mac[6]) {
    ToyDriverNetGetMac(Mac);
}

UINT32 HalNetGetIpAddress(void) {
    return ToyDriverNetGetIp();
}

void HalNetSetIpAddress(UINT32 Ip) {
    ToyDriverNetSetIp(Ip);
}

void HalNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    ToyDriverNetFormatIp(Ip, Buf, BufLen);
}

int HalNetParseIp(const char *Text, UINT32 *Ip) {
    return ToyDriverNetParseIp(Text, Ip);
}

int HalNetPing(const char *Host, int TimeoutMs) {
    return ToyDriverNetPing(Host, TimeoutMs);
}

void HalNetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    ToyDriverNetGetStats(TxDone, RxFrames);
}

int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex) {
    (void)Up;
    (void)Mbps;
    (void)FullDuplex;
    return 0;
}

int HalNetPrimaryKind(void) {
    return -1;
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    if (Write) {
        Write("e1000 note: n/a (not x86)\n");
    }
}

int HalNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen) {
    return ToyDriverNetSendIp(DstIp, Proto, Payload, PayloadLen);
}

UINT16 HalNetChecksum(const void *Data, UINTN Len) {
    return ToyDriverNetChecksum(Data, Len);
}

void HalNetSetLwipReceive(int Enable) {
    ToyDriverNetSetLwIpRx(Enable);
}
