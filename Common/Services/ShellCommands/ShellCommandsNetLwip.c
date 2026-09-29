/*
 * ShellCommandsNetLwip.c — PR-S3-shellnet-1：lwip on|status|dhcp（从 Net.c 搬家）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "LwIp.h"
#include "HalDevices.h"

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
    int Rc;

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        ConsoleWrite("lwip dhcp: net not available\n");
        return;
    }
    if (LwIpDhcpJobBusy()) {
        ConsoleWrite("lwip dhcp: busy\n");
        return;
    }
    /* 先 Hold：防 Worker 抢在 PromptAfterCommand 前打完并再出一次 toyos> */
    ConsoleJobHoldPrompt();
    Rc = LwIpDhcpEnqueue(12000);
    if (Rc == -1) {
        ConsoleWrite("lwip dhcp: busy\n");
        ConsoleJobReleasePrompt();
        return;
    }
    if (Rc != 0) {
        ConsoleWrite("lwip dhcp: start fail\n");
        ConsoleJobReleasePrompt();
        return;
    }
    ConsoleWrite("lwip dhcp: queued\n");
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

void ShellCommandsNetLwipRegister(void) {
#ifdef TOY_LWIP
    ConsoleRegister2("lwip", "on", "enable lwIP stack", CommandLwIp);
    ConsoleRegister2("lwip", "status", "lwIP status", CommandLwIp);
    ConsoleRegister2("lwip", "dhcp", "DHCP; wait IP then prompt", CommandLwIpDhcp);
    ConsoleRegister2("net", "dhcp", "DHCP; wait IP then prompt", CommandLwIpDhcp);
#endif
}
