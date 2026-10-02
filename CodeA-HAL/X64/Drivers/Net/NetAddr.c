/*
 * NetAddr.c — IP 文本与查询（PR-S-net-1）
 */
#include "NetPrivate.h"
#include "Udp.h"
#include "Tcp.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial.h"
#include "Debug.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "DriverNic.h"

void NetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    UINT8 B[4];
    int Pos = 0;
    int P;

    if (BufLen < 16) {
        return;
    }
    B[0] = (UINT8)((Ip >> 24) & 0xFF);
    B[1] = (UINT8)((Ip >> 16) & 0xFF);
    B[2] = (UINT8)((Ip >> 8) & 0xFF);
    B[3] = (UINT8)(Ip & 0xFF);
    for (P = 0; P < 4; P++) {
        UINT8 V = B[P];
        if (V >= 100) {
            Buf[Pos++] = '0' + V / 100;
            V %= 100;
            Buf[Pos++] = '0' + V / 10;
            Buf[Pos++] = '0' + V % 10;
        } else if (V >= 10) {
            Buf[Pos++] = '0' + V / 10;
            Buf[Pos++] = '0' + V % 10;
        } else {
            Buf[Pos++] = '0' + V;
        }
        if (P < 3) {
            Buf[Pos++] = '.';
        }
    }
    Buf[Pos] = 0;
}

int NetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10 + (UINT32)(*Text - '0');
            if (Val > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Val;
            Val = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Val;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

void NetInfo(void) {
    char IpBuf[20];
    if (!gNetOk) {
        DebugWrite("Net: not available\n");
        return;
    }
    NetFormatIp(gIp, IpBuf, sizeof(IpBuf));
    DebugWrite("Net: mac ");
    for (int i = 0; i < 6; i++) {
        DebugWrite(Uint8ToDecimal(gMac[i]));
        if (i < 5) {
            DebugWrite(":");
        }
    }
    DebugWrite(" ip ");
    DebugWrite(IpBuf);
    DebugWrite(" (QEMU user)\n");
}
