/*
 * ShellCommandsNetTcp.c — PR-S3-shellnet-1：tcp + ShellOnInterrupt（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Tcp.h"
#include "LwIp.h"
#include "HalDevices.h"

static int ArgIsStop(const char *S) {
    return S[0] == 's' && S[1] == 't' && S[2] == 'o' && S[3] == 'p' && S[4] == 0;
}

void ShellOnInterrupt(void) {
#ifdef TOY_LWIP
    if (LwIpActive()) {
        if (LwIpTcpListenStop() == 0) {
            ConsoleWrite("lwip: echo server stopped\n");
            ConsoleDiscardInput();
            ConsoleForceResumePrompt();
            return;
        }
    }
#endif
    if (TcpGetState() == TCP_LISTEN) {
        TcpListenStop();
        ConsoleWrite("tcp: echo server stopped\n");
        ConsoleDiscardInput();
        ConsoleForceResumePrompt();
        return;
    }
    /* 挂起中但已非 LISTEN（曾叠层 Suspend）：仍恢复提示符 */
    if (ConsolePromptSuspended()) {
        ConsoleWrite("tcp: echo server stopped\n");
        ConsoleDiscardInput();
        ConsoleForceResumePrompt();
        return;
    }
    ConsoleCancelInput();
}

static void CommandTcpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        ConsoleWrite("usage: tcp listen <port>|stop\n");
        return;
    }
    if (ArgIsStop(Argv[1])) {
#ifdef TOY_LWIP
        if (LwIpActive()) {
            if (LwIpTcpListenStop() != 0) {
                ConsoleWrite("tcplisten: not listening\n");
                return;
            }
        ConsoleWrite("lwip: echo server stopped\n");
            ConsoleDiscardInput();
            ConsoleForceResumePrompt();
            return;
        }
#endif
    if (TcpGetState() != TCP_LISTEN) {
            ConsoleWrite("tcplisten: not listening\n");
            return;
        }
        TcpListenStop();
        ConsoleWrite("tcp: echo server stopped\n");
        ConsoleDiscardInput();
        ConsoleForceResumePrompt();
        return;
    }
    for (const char *P = Argv[1]; *P; P++) {
        if (*P < '0' || *P > '9') {
            ConsoleWrite("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        if (LwIpTcpListen((UINT16)Port) != 0) {
            ConsoleWrite("tcplisten: failed\n");
            return;
        }
        if (!ConsolePromptSuspended()) {
            ConsoleSuspendPrompt();
        }
        ConsoleWrite("lwip: echo server on ");
        ConsoleWriteHex32(Port);
        ConsoleWrite("\n");
        return;
    }
#endif
    TcpListen((UINT16)Port);
    if (!ConsolePromptSuspended()) {
        ConsoleSuspendPrompt();
    }
    ConsoleWrite("tcp: echo server on ");
    ConsoleWriteHex32(Port);
    ConsoleWrite("\n");
}

static void CommandTcpStatus(int Argc, char **Argv) {
    char IpBuf[20];
    UINT32 Una;
    UINT32 Nxt;
    UINT32 BufLen;
    UINT16 PeerWnd;
    UINT8 Retrans;
    (void)Argc;
    (void)Argv;
#ifdef TOY_LWIP
    if (LwIpActive()) {
        ConsoleWrite("tcpstatus: lwIP active (builtin idle)\n");
        ConsoleWrite("  tcp listen=");
        ConsoleWriteHex32(LwIpTcpListenPort());
        ConsoleWrite(" udp bind=");
        ConsoleWriteHex32(LwIpUdpBoundPort());
        ConsoleWrite("\n");
        return;
    }
#endif
    TcpGetWindowStats(&Una, &Nxt, &BufLen, &PeerWnd, &Retrans);
    ConsoleWrite("tcp state=");
    ConsoleWriteHex32((UINT32)TcpGetState());
    ConsoleWrite(" local=");
    ConsoleWriteHex32(TcpLocalPort());
    ConsoleWrite(" peer=");
    HalNetFormatIp(TcpPeerIp(), IpBuf, sizeof(IpBuf));
    ConsoleWrite(IpBuf);
    ConsoleWrite(":");
    ConsoleWriteHex32(TcpPeerPort());
    ConsoleWrite("\n  snd_una=");
    ConsoleWriteHex32(Una);
    ConsoleWrite(" snd_nxt=");
    ConsoleWriteHex32(Nxt);
    ConsoleWrite(" buf=");
    ConsoleWriteHex32(BufLen);
    ConsoleWrite(" peer_wnd=");
    ConsoleWriteHex32(PeerWnd);
    ConsoleWrite(" retrans=");
    ConsoleWriteHex32(Retrans);
    ConsoleWrite("\n");
}

static void CommandTcpConnect(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    UINTN TextLen;
    if (Argc < 4) {
        ConsoleWrite("usage: tcp connect <ip> <port> <text>\n");
        return;
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        UINTN TextLen;
        int Ret;

        if (!HalNetReady()) {
            ConsoleWrite("Net: not available\n");
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
        if (Port == 0 || Port > 65535) {
            ConsoleWrite("bad port\n");
            return;
        }
        TextLen = 0;
        while (Argv[3][TextLen]) {
            TextLen++;
        }
        Ret = LwIpTcpConnectSend(Ip, (UINT16)Port, Argv[3], TextLen, 3000);
        if (Ret == 0) {
            ConsoleWrite("tcpconnect: done\n");
        } else if (Ret == -2) {
            ConsoleWrite("tcpconnect: timeout\n");
        } else if (Ret == -3) {
            ConsoleWrite("tcpconnect: send failed\n");
        } else {
            ConsoleWrite("tcpconnect: syn failed\n");
            ConsoleWrite("hint: on host run nc -l ");
            ConsoleWriteHex32(Port);
            ConsoleWrite(" first\n");
        }
        return;
    }
#endif
    if (TcpGetState() == TCP_LISTEN) {
        ConsoleWrite("tcpconnect: closes tcplisten (single TCP slot)\n");
    }
    if (!HalNetReady()) {
        ConsoleWrite("Net: not available\n");
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
    if (Port == 0 || Port > 65535) {
        ConsoleWrite("bad port\n");
        return;
    }
    if (TcpConnect(Ip, (UINT16)Port) != 0) {
        ConsoleWrite("tcpconnect: syn failed\n");
        return;
    }
    {
        int Tries = 3000;
        while (Tries-- > 0 && TcpGetState() == TCP_SYN_SENT) {
            HalNetPoll();
            TcpPoll();
        }
    }
    if (TcpGetState() != TCP_ESTABLISHED) {
        ConsoleWrite("tcpconnect: timeout\n");
        return;
    }
    TextLen = 0;
    while (Argv[3][TextLen]) {
        TextLen++;
    }
    if (TcpSend(Argv[3], TextLen) != 0) {
        ConsoleWrite("tcpconnect: send failed\n");
        return;
    }
    {
        int Tries = 3000;
        while (Tries-- > 0 && TcpGetState() == TCP_ESTABLISHED) {
            HalNetPoll();
            TcpPoll();
        }
    }
    ConsoleWrite("tcpconnect: done\n");
}

void ShellCommandsNetTcpRegister(void) {
    ConsoleRegister2("tcp", "listen", "TCP echo server", CommandTcpListen);
    ConsoleRegister2("tcp", "connect", "TCP connect and send", CommandTcpConnect);
    ConsoleRegister2("tcp", "status", "TCP connection status", CommandTcpStatus);
    ConsoleRegisterAliasLine("tcplisten", "tcp", "listen");
    ConsoleRegisterAliasLine("tcpconnect", "tcp", "connect");
    ConsoleRegisterAliasLine("tcpstatus", "tcp", "status");
}
