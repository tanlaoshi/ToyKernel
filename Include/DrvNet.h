/*
 * DrvNet.h — Net 类适配（PR-D3）
 *
 * 驱动 Bind 时调用 ToyDrvNetAttach；Common 仍只见 HalNet*。
 */
#ifndef DRV_NET_H
#define DRV_NET_H

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

int ToyDrvNetAttach(const NET_BACKEND *Backend);
int ToyDrvNetReady(void);

void ToyDrvNetPoll(void);
void ToyDrvNetGetMac(UINT8 Mac[6]);
UINT32 ToyDrvNetGetIp(void);
void ToyDrvNetFormatIp(UINT32 Ip, char *Buf, int BufLen);
int ToyDrvNetParseIp(const char *Text, UINT32 *Ip);
int ToyDrvNetPing(const char *Host, int TimeoutMs);
void ToyDrvNetGetStats(UINT32 *TxDone, UINT32 *RxFrames);
int ToyDrvNetSendIp(UINT32 DstIp, UINT8 Proto, const void *Payload, UINTN PayloadLen);
UINT16 ToyDrvNetChecksum(const void *Data, UINTN Len);
void ToyDrvNetSetLwIpRx(int Enable);

#endif
