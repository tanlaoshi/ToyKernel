/*
 * VirtioNetAddr.c — IP 文本格式（PR-S-virtionet-virt-1）
 */
#include "VirtioNet.h"
#include "VirtioNetPrivate.h"
#include "VirtioMmio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#include "Driver.h"
#include "DriverNet.h"
#include "ToySerialLog.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif

void VirtioNetFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    UINT8 B[4];
    int Pos = 0;
    int P;

    if (!Buf || BufLen < 16) {
        return;
    }
    B[0] = (UINT8)((Ip >> 24) & 0xFF);
    B[1] = (UINT8)((Ip >> 16) & 0xFF);
    B[2] = (UINT8)((Ip >> 8) & 0xFF);
    B[3] = (UINT8)(Ip & 0xFF);
    for (P = 0; P < 4; P++) {
        UINT8 V = B[P];
        if (V >= 100) {
            Buf[Pos++] = (char)('0' + V / 100);
            V = (UINT8)(V % 100);
            Buf[Pos++] = (char)('0' + V / 10);
            Buf[Pos++] = (char)('0' + V % 10);
        } else if (V >= 10) {
            Buf[Pos++] = (char)('0' + V / 10);
            Buf[Pos++] = (char)('0' + V % 10);
        } else {
            Buf[Pos++] = (char)('0' + V);
        }
        if (P < 3) {
            Buf[Pos++] = '.';
        }
    }
    Buf[Pos] = 0;
}

int VirtioNetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10u + (UINT32)(*Text - '0');
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
