/*
 * ShellCmdNet.c — PR-S-shell-split-2：net / ping / dns / udp / tcp / lwip
 *
 * 从 ShellCommands.c 原样搬家；不改语义。含 ShellOnInterrupt（Ctrl-C 停 echo）。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "NetConfig.h"
#include "HalDevices.h"

static void CommandNet(int Argc, char **Argv) {
    char IpBuf[20];
    char Hex[3];
    UINT8 Mac[6];
    int i;
    static const char Digits[] = "0123456789ABCDEF";

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        ConsoleWrite("Net: not available (no virtio-net)\n");
        return;
    }
    NetConfigEnsure();
    HalNetGetMacAddress(Mac);
    HalNetFormatIp(HalNetGetIpAddress(), IpBuf, sizeof(IpBuf));
    ConsoleWrite("mac ");
    for (i = 0; i < 6; i++) {
        Hex[0] = Digits[(Mac[i] >> 4) & 0xF];
        Hex[1] = Digits[Mac[i] & 0xF];
        Hex[2] = 0;
        ConsoleWrite(Hex);
        if (i < 5) {
            ConsoleWrite(":");
        }
    }
    ConsoleWrite("\nip  ");
    ConsoleWrite(IpBuf);
    ConsoleWrite(" mask ");
    HalNetFormatIp(NetConfigGetMask(), IpBuf, sizeof(IpBuf));
    ConsoleWrite(IpBuf);
    ConsoleWrite("\ngw  ");
    if (NetConfigGetGw() == 0) {
        ConsoleWrite("unset");
    } else {
        HalNetFormatIp(NetConfigGetGw(), IpBuf, sizeof(IpBuf));
        ConsoleWrite(IpBuf);
    }
    ConsoleWrite(" dns ");
    if (NetConfigGetDns() == 0) {
        ConsoleWrite("unset");
    } else {
        HalNetFormatIp(NetConfigGetDns(), IpBuf, sizeof(IpBuf));
        ConsoleWrite(IpBuf);
    }
    ConsoleWrite("\n");
    {
        int Up = 0;
        UINT32 Mbps = 0;
        int Fd = 0;

        if (HalNetGetLinkInfo(&Up, &Mbps, &Fd)) {
            ConsoleWrite("link ");
            ConsoleWrite(Up ? "up" : "down");
            if (Up) {
                if (Mbps == 1000u) {
                    ConsoleWrite(" 1000");
                } else if (Mbps == 100u) {
                    ConsoleWrite(" 100");
                } else if (Mbps == 10u) {
                    ConsoleWrite(" 10");
                } else {
                    ConsoleWrite(" ?");
                }
                ConsoleWrite(Fd ? "/FD" : "/HD");
            }
            ConsoleWrite("\n");
        }
    }
    {
        UINT32 TxDone = 0;
        UINT32 RxFrames = 0;
        HalNetGetStats(&TxDone, &RxFrames);
        ConsoleWrite("stats tx_done=");
        ConsoleWriteHex32(TxDone);
        ConsoleWrite(" rx_frames=");
        ConsoleWriteHex32(RxFrames);
        ConsoleWrite("\n");
    }
#ifdef TOY_LWIP
    ConsoleWrite("stack ");
    ConsoleWrite(LwIpActive() ? "lwip (RX unified)\n" : "builtin (run lwip on)\n");
#endif
}

static void CommandPing(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: ping <ip>\n");
        return;
    }
    if (!HalNetReady()) {
        ConsoleWrite("Net: not available\n");
        return;
    }
    ConsoleWrite("ping ");
    ConsoleWrite(Argv[1]);
    ConsoleWrite(" ...\n");
#ifdef TOY_LWIP
    if (LwIpActive()) {
        UINT32 Ip;
        if (HalNetParseIp(Argv[1], &Ip) != 0) {
            ConsoleWrite("bad ip\n");
            return;
        }
        if (LwIpPing(Ip, 3000) == 0) {
            ConsoleWrite("reply from ");
            ConsoleWrite(Argv[1]);
            ConsoleWrite("\n");
        } else {
            ConsoleWrite("no reply\n");
        }
        return;
    }
#endif
    if (HalNetPing(Argv[1], 3000) == 0) {
        ConsoleWrite("reply from ");
        ConsoleWrite(Argv[1]);
        ConsoleWrite("\n");
    } else {
        ConsoleWrite("no reply\n");
    }
}

#ifdef TOY_LWIP
/* PR-N-dns：dns <name|ip> → A / 字面量 */
static void CommandDns(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 LiteralIp;
    int Rc;
    int IsLiteral;
    char IpBuf[16];

    if (Argc < 2) {
        ConsoleWrite("usage: dns <name|ip>\n");
        return;
    }
    if (!HalNetReady()) {
        ConsoleWrite("dns: net not available\n");
        return;
    }
    IsLiteral = (HalNetParseIp(Argv[1], &LiteralIp) == 0);
    if (!LwIpActive()) {
        if (IsLiteral) {
            HalNetFormatIp(LiteralIp, IpBuf, (int)sizeof(IpBuf));
            ConsoleWrite(Argv[1]);
            ConsoleWrite(" -> ");
            ConsoleWrite(IpBuf);
            ConsoleWrite(" (literal; lwip on for names)\n");
            return;
        }
        ConsoleWrite("dns: run lwip on for name lookup\n");
        return;
    }
    Rc = LwIpDnsLookup(Argv[1], &Ip, 5000);
    if (Rc != 0) {
        ConsoleWrite("dns: fail errno=");
        ConsoleWriteHex32((UINT32)(-Rc));
        ConsoleWrite("\n");
        return;
    }
    HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
    ConsoleWrite(Argv[1]);
    ConsoleWrite(" -> ");
    ConsoleWrite(IpBuf);
    ConsoleWrite(IsLiteral ? " (literal)\n" : " (A)\n");
}
#endif

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

#ifdef TOY_LWIP
static void ShellLwIpPrintStatus(void) {
    ConsoleWrite("lwip: on (RX unified; ping/tcp/udp via lwIP)\n");
    ConsoleWrite("  tcp listen=");
    ConsoleWriteHex32(LwIpTcpListenPort());
    ConsoleWrite(" udp bind=");
    ConsoleWriteHex32(LwIpUdpBoundPort());
    ConsoleWrite(LwIpDhcpRunning() ? " dhcp=on\n" : " dhcp=off\n");
}

static void CommandLwIpDhcp(int Argc, char **Argv) {
    char IpBuf[16];

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        ConsoleWrite("lwip dhcp: net not available\n");
        return;
    }
    ConsoleWrite("lwip dhcp: requesting...\n");
    if (LwIpDhcpStart(8000) != 0) {
        ConsoleWrite("lwip dhcp: no offer (kept static)\n");
        return;
    }
    HalNetFormatIp(NetConfigGetIp(), IpBuf, (int)sizeof(IpBuf));
    ConsoleWrite("lwip dhcp: ok ip=");
    ConsoleWrite(IpBuf);
    ConsoleWrite("\n");
}

static void CommandLwIp(int Argc, char **Argv) {
    const char *Word;

    /* 正统：lwip on|status|dhcp → Argv[0]=二级；旧：lwip on → Argv[1] */
    if (Argc >= 1 && Argv[0][0] == 'o' && Argv[0][1] == 'n' && Argv[0][2] == 0) {
        Word = Argv[0];
    } else if (Argc >= 1 && Argv[0][0] == 's') {
        Word = Argv[0];
    } else if (Argc >= 1 && Argv[0][0] == 'd') {
        Word = Argv[0];
    } else if (Argc >= 2) {
        Word = Argv[1];
    } else {
        ConsoleWrite("usage: lwip on|status|dhcp\n");
        return;
    }
    if (Word[0] == 'o' && Word[1] == 'n' && Word[2] == 0) {
        if (LwIpActive()) {
            ConsoleWrite("lwip: already on\n");
            return;
        }
        if (LwIpInit() != 0) {
            ConsoleWrite("lwip: init failed\n");
            return;
        }
        ShellLwIpPrintStatus();
        return;
    }
    if (Word[0] == 'd') {
        CommandLwIpDhcp(Argc, Argv);
        return;
    }
    if (Word[0] == 's') {
        if (!LwIpActive()) {
            ConsoleWrite("lwip: off (builtin stack; run lwip on)\n");
            return;
        }
        ShellLwIpPrintStatus();
        return;
    }
    ConsoleWrite("usage: lwip on|status|dhcp\n");
}
#endif

void ShellCmdNetRegister(void) {
    ConsoleRegister2("show", "network", "network info", CommandNet);
    ConsoleRegister("net", "network info; net config …", CommandNet);
    ConsoleRegisterAliasLine("network", "show", "network");
    ShellCmdNetAddrRegister();
    ConsoleRegister("ping", "ICMP echo", CommandPing);
#ifdef TOY_LWIP
    ConsoleRegister("dns", "DNS A / IPv4 literal (PR-N-dns)", CommandDns);
#endif
    ConsoleRegister2("udp", "listen", "bind UDP port", CommandUdpListen);
    ConsoleRegister2("udp", "send", "send UDP datagram", CommandUdpSend);
    ConsoleRegisterAliasLine("udplisten", "udp", "listen");
    ConsoleRegisterAliasLine("udpsend", "udp", "send");
    ConsoleRegister2("tcp", "listen", "TCP echo server", CommandTcpListen);
    ConsoleRegister2("tcp", "connect", "TCP connect and send", CommandTcpConnect);
    ConsoleRegister2("tcp", "status", "TCP connection status", CommandTcpStatus);
    ConsoleRegisterAliasLine("tcplisten", "tcp", "listen");
    ConsoleRegisterAliasLine("tcpconnect", "tcp", "connect");
    ConsoleRegisterAliasLine("tcpstatus", "tcp", "status");
#ifdef TOY_LWIP
    ConsoleRegister2("lwip", "on", "enable lwIP stack", CommandLwIp);
    ConsoleRegister2("lwip", "status", "lwIP status", CommandLwIp);
    ConsoleRegister2("lwip", "dhcp", "DHCP request (8s)", CommandLwIpDhcp);
    ConsoleRegister2("net", "dhcp", "DHCP request (alias)", CommandLwIpDhcp);
#endif
}
