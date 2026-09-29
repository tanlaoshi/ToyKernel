/*
 * HalDevicesNet.c — x86：Net 门面（PR-S3-haldev-1）
 */
#include "Hal.h"
#include "DriverNet.h"
#include "Net.h"
#include "E1000.h"

int HalNetInit(void) {
    return NetInit();
}

int HalNetReady(void) {
    return ToyDriverNetReady();
}

UINT32 HalNetNicEpoch(void) {
    return NetNicEpoch();
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

/* PR-N-nic：链路走已挂 NIC_L2（e1000 等）；virtio / 无 L2 → 0 */
int HalNetGetLinkInfo(int *Up, UINT32 *Mbps, int *FullDuplex) {
    if (NetNicGetLink(Up, Mbps, FullDuplex) != 0) {
        return 0;
    }
    return 1;
}

void HalNetDumpNicNote(void (*Write)(const char *Text)) {
    E1000DumpNote(Write);
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
