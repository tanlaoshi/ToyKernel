/*
 * ShellCommandsNetUdp.c — PR-S3-shellnet-1：udp listen / send（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Udp.h"
#include "LwIp.h"
#include "HalDevices.h"

static void CommandUdpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        ConsoleWrite("usage: udplisten <port>\n");
        return;
    }
    for (const char *P = Argv[1]; *P; P++) {
        if (*P < '0' || *P > '9') {
            ConsoleWrite("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
    if (Port == 0 || Port > 65535) {
        ConsoleWrite("bad port\n");
        return;
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        if (LwIpUdpBind((UINT16)Port) != 0) {
            ConsoleWrite("udplisten: failed\n");
            return;
        }
        ConsoleWrite("lwip: udp listening ");
        ConsoleWriteHex32(Port);
        ConsoleWrite("\n");
        return;
    }
#endif
    UdpBind((UINT16)Port);
    ConsoleWrite("udp: listening ");
    ConsoleWriteHex32(Port);
    ConsoleWrite("\n");
}

static void CommandUdpSend(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    if (Argc < 4) {
        ConsoleWrite("usage: udpsend <ip> <port> <text>\n");
        return;
    }
    if (HalNetParseIp(Argv[1], &Ip) != 0) {
        ConsoleWrite("bad ip\n");
        return;
    }
    for (const char *P = Argv[2]; *P; P++) {
        if (*P < '0' || *P > '9') {
            ConsoleWrite("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
    {
        UINTN Len = 0;
        while (Argv[3][Len]) {
            Len++;
        }
#ifdef TOY_LWIP
        if (LwIpActive()) {
            if (LwIpUdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
                ConsoleWrite("udpsend failed\n");
            } else {
                ConsoleWrite("udp: sent\n");
            }
            return;
        }
#endif
        if (UdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
            ConsoleWrite("udpsend failed\n");
        } else {
            ConsoleWrite("udp: sent\n");
        }
    }
}

void ShellCommandsNetUdpRegister(void) {
    ConsoleRegister2("udp", "listen", "bind UDP port", CommandUdpListen);
    ConsoleRegister2("udp", "send", "send UDP datagram", CommandUdpSend);
    ConsoleRegisterAliasLine("udplisten", "udp", "listen");
    ConsoleRegisterAliasLine("udpsend", "udp", "send");
}
