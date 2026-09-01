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
#include "VirtualMemory.h"

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
    ProcessRunDemo();
}

static void CommandExec(int Argc, char **Argv) {
    if (Argc < 2) {
        ConsoleWrite("usage: exec <file>\n");
        return;
    }
    ProcessExec(Argv[1]);
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
        ConsoleWrite(" cr3=");
        ConsoleHex64(T->Cr3);
        ConsoleWrite(" rip=");
        ConsoleHex64(SchedulerTaskRip(T));
        ConsoleWrite(" ticks=");
        ConsoleHex32(T->Ticks);
        if (SchedulerCurrent() == T) {
            ConsoleWrite(" *");
        }
        ConsoleWrite("\n");
    }
    ConsoleWrite("worker loops=");
    ConsoleHex32(WorkerLoopCount());
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
        if (UdpSend(Ip, (UINT16)Port, Argv[3], Len) != 0) {
            ConsoleWrite("udpsend failed\n");
        } else {
            ConsoleWrite("udp: sent\n");
        }
    }
}

static void CommandTcpListen(int Argc, char **Argv) {
    UINT32 Port = 0;
    if (Argc < 2) {
        ConsoleWrite("usage: tcplisten <port>\n");
        return;
    }
    for (const char *P = Argv[1]; *P; P++) {
        if (*P < '0' || *P > '9') {
            ConsoleWrite("bad port\n");
            return;
        }
        Port = Port * 10 + (UINT32)(*P - '0');
    }
    TcpListen((UINT16)Port);
    ConsoleWrite("tcp: echo server on ");
    ConsoleHex32(Port);
    ConsoleWrite("\n");
}

static void CommandTcpStatus(int Argc, char **Argv) {
    char IpBuf[20];
    (void)Argc;
    (void)Argv;
    ConsoleWrite("tcp state=");
    ConsoleHex32((UINT32)TcpGetState());
    ConsoleWrite(" local=");
    ConsoleHex32(TcpLocalPort());
    ConsoleWrite(" peer=");
    HalNetFormatIp(TcpPeerIp(), IpBuf, sizeof(IpBuf));
    ConsoleWrite(IpBuf);
    ConsoleWrite(":");
    ConsoleHex32(TcpPeerPort());
    ConsoleWrite("\n");
}

void ShellCommandsRegister(void) {
    ConsoleRegister("info", "boot framebuffer info", CommandInfo);
    ConsoleRegister("ps", "list tasks", CommandPs);
    ConsoleRegister("mem", "physical memory stats", CommandMem);
    ConsoleRegister("memtest", "alloc/verify/free one page", CommandMemtest);
    ConsoleRegister("runuser", "run embedded hello ELF", CommandRunuser);
    ConsoleRegister("exec", "load ELF from FAT", CommandExec);
    ConsoleRegister("net", "network info", CommandNet);
    ConsoleRegister("ping", "ICMP echo", CommandPing);
    ConsoleRegister("udplisten", "bind UDP port", CommandUdpListen);
    ConsoleRegister("udpsend", "send UDP datagram", CommandUdpSend);
    ConsoleRegister("tcplisten", "TCP echo server", CommandTcpListen);
    ConsoleRegister("tcpstatus", "TCP connection status", CommandTcpStatus);
}
