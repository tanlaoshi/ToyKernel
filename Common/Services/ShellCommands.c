/*
 * ShellCommands.c — Shell 扩展命令（mem / exec / net / ping 等）
 */
#include "ShellCommands.h"
#include "BootInfo.h"
#include "Console.h"
#include "PhysicalMemory.h"
#include "Process.h"
#include "Scheduler.h"
#include "Tasks.h"
#include "Hal.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "VirtualMemory.h"
#include "Gui.h"

static void CommandInfo(int Argc, char **Argv) {
    const BOOT_INFO *Info = BootInfoGet();
    (void)Argc;
    (void)Argv;
    if (!Info) {
        ConsoleWrite("video (no boot info)\n");
        return;
    }
    ConsoleWrite("video ");
    ConsoleHex32(Info->HorizontalResolution);
    ConsoleWrite(" x ");
    ConsoleHex32(Info->VerticalResolution);
    ConsoleWrite(" fb=");
    ConsoleHex64(Info->FrameBufferBase);
    ConsoleWrite("\n");
}

static void CommandMem(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("physical memory\n  free  ");
    ConsoleHex64(PhysicalMemoryFreePageCount() << PAGE_SHIFT);
    ConsoleWrite(" bytes (");
    ConsoleHex32((UINT32)PhysicalMemoryFreePageCount());
    ConsoleWrite(" pages)\n  total ");
    ConsoleHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    ConsoleWrite(" bytes tracked\n");
}

static void CommandMemtest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    UINT64 Before = PhysicalMemoryFreePageCount();
    void *Page = PhysicalMemoryAllocatePage();
    if (Page == 0) {
        ConsoleWrite("memtest: alloc failed\n");
        return;
    }
    UINT8 *Bytes = (UINT8 *)Page;
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        Bytes[i] = (UINT8)i;
    }
    for (int i = 0; i < (int)PAGE_SIZE; i++) {
        if (Bytes[i] != (UINT8)i) {
            ConsoleWrite("memtest: verify failed at ");
            ConsoleHex32((UINT32)i);
            ConsoleWrite("\n");
            PhysicalMemoryFreePage(Page);
            return;
        }
    }
    ConsoleWrite("memtest: page ");
    ConsoleHex64((UINT64)(UINTN)Page);
    ConsoleWrite(" ok, freeing\n");
    PhysicalMemoryFreePage(Page);
    ConsoleWrite("memtest: free pages ");
    ConsoleHex32((UINT32)Before);
    ConsoleWrite(" -> ");
    ConsoleHex32((UINT32)PhysicalMemoryFreePageCount());
    ConsoleWrite("\n");
}

static void CommandRunuser(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    if (ProcessRunDemo() == 0) {
        ConsoleWaitPrompt();
    }
}

static void CommandExec(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: exec <file>\n");
        return;
    }
    if (ProcessExec(Argv[1]) == 0) {
        ConsoleWaitPrompt();
    }
}

static void CommandPs(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    for (int i = 0; i < MAX_TASKS; i++) {
        const TASK *T = SchedulerTaskByIndex(i);
        if (!T) {
            continue;
        }
        ConsoleWrite("  ");
        ConsoleWrite(T->Name);
        if (T->IsUser) {
            ConsoleWrite(" user");
        } else {
            ConsoleWrite(" kern");
        }
        if (T->State == TASK_ZOMBIE) {
            ConsoleWrite(" zombie");
        } else if (T->State == TASK_BLOCKED) {
            ConsoleWrite(" blocked");
        }
        ConsoleWrite(" root=");
        ConsoleHex64(T->PageRoot);
        ConsoleWrite(" rip=");
        ConsoleHex64(SchedulerTaskRip(T));
        ConsoleWrite(" ticks=");
        ConsoleHex32(T->Ticks);
        ConsoleWrite(" cpu=");
        ConsoleHex32((UINT32)T->OnCpu);
        ConsoleWrite(" home=");
        ConsoleHex32((UINT32)T->HomeCpu);
        if (SchedulerCurrent() == T) {
            ConsoleWrite(" *");
        }
        ConsoleWrite("\n");
    }
    ConsoleWrite("worker loops=");
    ConsoleHex32(WorkerLoopCount());
    ConsoleWrite(" steals=");
    ConsoleHex64(SchedulerStealCount());
    ConsoleWrite("\n");
}

static void CommandNet(int Argc, char **Argv) {
    char IpBuf[20];
    char Hex[3];
    UINT8 Mac[6];
    int i;
    static const char Digits[] = "0123456789ABCDEF";

    (void)Argc;
    (void)Argv;
    if (!HalNetReady()) {
        ConsoleWrite("net: not available (no virtio-net)\n");
        return;
    }
    HalNetGetMac(Mac);
    HalNetFormatIp(HalNetGetIp(), IpBuf, sizeof(IpBuf));
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
    ConsoleWrite("/24 gw 10.0.2.2 (QEMU user)\n");
    {
        UINT32 TxDone = 0;
        UINT32 RxFrames = 0;
        HalNetGetStats(&TxDone, &RxFrames);
        ConsoleWrite("stats tx_done=");
        ConsoleHex32(TxDone);
        ConsoleWrite(" rx_frames=");
        ConsoleHex32(RxFrames);
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
        ConsoleWrite("net: not available\n");
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
        ConsoleHex32(Port);
        ConsoleWrite("\n");
        return;
    }
#endif
    UdpBind((UINT16)Port);
    ConsoleWrite("udp: listening ");
    ConsoleHex32(Port);
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
            ConsoleResumePrompt();
            return;
        }
    }
#endif
    if (TcpGetState() == TCP_LISTEN) {
        TcpListenStop();
        ConsoleWrite("tcp: echo server stopped\n");
        ConsoleResumePrompt();
        return;
    }
    ConsoleCancelInput();
}

static void CommandTcpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        ConsoleWrite("usage: tcplisten <port>|stop\n");
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
            ConsoleResumePrompt();
            return;
        }
#endif
        if (TcpGetState() != TCP_LISTEN) {
            ConsoleWrite("tcplisten: not listening\n");
            return;
        }
        TcpListenStop();
        ConsoleWrite("tcp: echo server stopped\n");
        ConsoleResumePrompt();
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
        ConsoleSuspendPrompt();
        ConsoleWrite("lwip: echo server on ");
        ConsoleHex32(Port);
        ConsoleWrite("\n");
        return;
    }
#endif
    TcpListen((UINT16)Port);
    ConsoleSuspendPrompt();
    ConsoleWrite("tcp: echo server on ");
    ConsoleHex32(Port);
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
        ConsoleHex32(LwIpTcpListenPort());
        ConsoleWrite(" udp bind=");
        ConsoleHex32(LwIpUdpBoundPort());
        ConsoleWrite("\n");
        return;
    }
#endif
    TcpGetWindowStats(&Una, &Nxt, &BufLen, &PeerWnd, &Retrans);
    ConsoleWrite("tcp state=");
    ConsoleHex32((UINT32)TcpGetState());
    ConsoleWrite(" local=");
    ConsoleHex32(TcpLocalPort());
    ConsoleWrite(" peer=");
    HalNetFormatIp(TcpPeerIp(), IpBuf, sizeof(IpBuf));
    ConsoleWrite(IpBuf);
    ConsoleWrite(":");
    ConsoleHex32(TcpPeerPort());
    ConsoleWrite("\n  snd_una=");
    ConsoleHex32(Una);
    ConsoleWrite(" snd_nxt=");
    ConsoleHex32(Nxt);
    ConsoleWrite(" buf=");
    ConsoleHex32(BufLen);
    ConsoleWrite(" peer_wnd=");
    ConsoleHex32(PeerWnd);
    ConsoleWrite(" retrans=");
    ConsoleHex32(Retrans);
    ConsoleWrite("\n");
}

static void CommandTcpConnect(int Argc, char **Argv) {
    UINT32 Ip;
    UINT32 Port = 0;
    UINTN TextLen;
    if (Argc < 4) {
        ConsoleWrite("usage: tcpconnect <ip> <port> <text>\n");
        return;
    }
#ifdef TOY_LWIP
    if (LwIpActive()) {
        UINTN TextLen;
        int Ret;

        if (!HalNetReady()) {
            ConsoleWrite("net: not available\n");
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
            ConsoleHex32(Port);
            ConsoleWrite(" first\n");
        }
        return;
    }
#endif
    if (TcpGetState() == TCP_LISTEN) {
        ConsoleWrite("tcpconnect: closes tcplisten (single TCP slot)\n");
    }
    if (!HalNetReady()) {
        ConsoleWrite("net: not available\n");
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
    ConsoleHex32(LwIpTcpListenPort());
    ConsoleWrite(" udp bind=");
    ConsoleHex32(LwIpUdpBoundPort());
    ConsoleWrite("\n");
}

static void CommandLwIp(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: lwip on|status\n");
        return;
    }
    if (Argv[1][0] == 'o' && Argv[1][1] == 'n' && Argv[1][2] == 0) {
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
    if (Argv[1][0] == 's') {
        if (!LwIpActive()) {
            ConsoleWrite("lwip: off (builtin stack; run lwip on)\n");
            return;
        }
        ShellLwIpPrintStatus();
        return;
    }
    ConsoleWrite("usage: lwip on|status\n");
}
#endif

static void CommandReboot(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    ConsoleWrite("rebooting...\n");
    HalConsoleWriteSerial("shell: reboot\n");
    HalCpuReboot();
}

static void CommandShell(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenShell();
    if (Idx < 0) {
        ConsoleWrite("shell: no free window\n");
        return;
    }
    ConsoleOnShellOpened();
}

static void CommandSettings(int Argc, char **Argv) {
    int Idx;

    (void)Argc;
    (void)Argv;
    Idx = GuiOpenSettings();
    if (Idx < 0) {
        ConsoleWrite("settings: no free window\n");
    }
}

void ShellCommandsRegister(void) {
    ConsoleRegister("info", "boot framebuffer info", CommandInfo);
    ConsoleRegister("ps", "list tasks", CommandPs);
    ConsoleRegister("mem", "physical memory stats", CommandMem);
    ConsoleRegister("memtest", "alloc/verify/free one page", CommandMemtest);
    ConsoleRegister("runuser", "run embedded hello ELF", CommandRunuser);
    ConsoleRegister("exec", "load ELF from FAT", CommandExec);
    ConsoleRegister("shell", "open Shell window", CommandShell);
    ConsoleRegister("settings", "open Settings window", CommandSettings);
    ConsoleRegister("reboot", "reset CPU (QEMU display: quit+./run-split.sh)", CommandReboot);
    ConsoleRegister("net", "network info", CommandNet);
    ConsoleRegister("ping", "ICMP echo", CommandPing);
    ConsoleRegister("udplisten", "bind UDP port", CommandUdpListen);
    ConsoleRegister("udpsend", "send UDP datagram", CommandUdpSend);
    ConsoleRegister("tcplisten", "TCP echo server", CommandTcpListen);
    ConsoleRegister("tcpconnect", "TCP connect and send", CommandTcpConnect);
    ConsoleRegister("tcpstatus", "TCP connection status", CommandTcpStatus);
#ifdef TOY_LWIP
    ConsoleRegister("lwip", "lwIP stack (lwip on)", CommandLwIp);
#endif
}
