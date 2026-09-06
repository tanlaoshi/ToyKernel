/*
 * DriverNet.h — Net 类适配（PR-D3）
 *
 * 驱动 Bind 时调用 ToyDriverNetAttach；Common 仍只见 HalNet*。
 */
#ifndef DRIVER_NET_H
#define DRIVER_NET_H

#include "BootTypes.h"

typedef struct {
    int (*Ready)(void);
    void (*Poll)(void);
    void (*GetMac)(UINT8 Mac[6]);
    UINT32 (*GetIp)(void);
    void (*FormatIp)(UINT32 Ip, char *Buf, int BufLen);
    int (*ParseIp)(const char *Text, UINT32 *Ip);
    int (*Ping)(const char *Host, int TimeoutMs);
    void (*GetStats)(UINT32 *TxDone, UINT32 *RxFrames);
    int (*SendIp)(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
    UINT16 (*Checksum)(const void *Data, UINTN Len);
    void (*SetLwIpRx)(int Enable);
} NET_BACKEND;

int ToyDriverNetAttach(const NET_BACKEND *Backend);
int ToyDriverNetReady(void);

void ToyDriverNetPoll(void);
void ToyDriverNetGetMac(UINT8 Mac[6]);
UINT32 ToyDriverNetGetIp(void);
void ToyDriverNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int ToyDriverNetParseIp(const char *Text, UINT32 *Ip);
int ToyDriverNetPing(const char *Host, int TimeoutMs);
void ToyDriverNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int ToyDriverNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 ToyDriverNetChecksum(const void *Data, UINTN Len);
void ToyDriverNetSetLwIpRx(int Enable);

#endif
