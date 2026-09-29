/*
 * ShellCommandsNet.c — PR-S3-shellnet-1：net / ping / dns + 注册汇总
 *
 * udp/tcp/lwip 见 ShellCommandsNet{Udp,Tcp,Lwip}.c；地址见 NetAddr.c。
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Hal.h"
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
        ConsoleWrite("Net: not available (no link / wifi)\n");
        return;
    }
    {
        int Up = 0;
        UINT32 Mbps = 0;
        int Fd = 0;

        if (HalNetGetLinkInfo(&Up, &Mbps, &Fd) && !Up) {
            ConsoleWrite("Net: link down\n");
            return;
        }
    }
    ConsoleWrite("ping ");
    ConsoleWrite(Argv[1]);
    ConsoleWrite(" ...\n");
#ifdef TOY_LWIP
    {
        UINT32 Ip;

        if (HalNetParseIp(Argv[1], &Ip) != 0) {
            ConsoleWrite("bad ip\n");
            return;
        }
        if (!LwIpActive() && LwIpInit() != 0) {
            ConsoleWrite("lwip: init failed\n");
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
#else
    if (HalNetPing(Argv[1], 3000) == 0) {
        ConsoleWrite("reply from ");
        ConsoleWrite(Argv[1]);
        ConsoleWrite("\n");
    } else {
        ConsoleWrite("no reply\n");
    }
#endif
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

void ShellCommandsNetRegister(void) {
    ConsoleRegister2("show", "network", "network info", CommandNet);
    ConsoleRegister("net", "network info; net config …", CommandNet);
    ConsoleRegisterAliasLine("network", "show", "network");
    ShellCommandsNetAddrRegister();
    ConsoleRegister("ping", "ICMP echo", CommandPing);
#ifdef TOY_LWIP
    ConsoleRegister("dns", "DNS A / IPv4 literal (PR-N-dns)", CommandDns);
#endif
    ShellCommandsNetUdpRegister();
    ShellCommandsNetTcpRegister();
    ShellCommandsNetLwipRegister();
}
