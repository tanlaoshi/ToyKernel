/*
 * chatd.c — Ubuntu 侧 TCP 行聊天服务端（PR-CHAT-1）
 *
 * 协议：局域网商店与聊天.md §3.3（TCP 明文 :9090，一行一条 \n，≤200B）。
 * 用法：./chatd [port]；另开终端 nc 127.0.0.1 9090 互发。
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHAT_PORT_DEFAULT 9090
#define CHAT_LINE_MAX     200

static int ListenPort(unsigned Port) {
    int Fd;
    int On = 1;
    struct sockaddr_in Addr;

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        perror("socket");
        return -1;
    }
    if (setsockopt(Fd, SOL_SOCKET, SO_REUSEADDR, &On, sizeof(On)) != 0) {
        perror("setsockopt");
        close(Fd);
        return -1;
    }
    memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
    Addr.sin_port = htons((uint16_t)Port);
    if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) != 0) {
        perror("bind");
        close(Fd);
        return -1;
    }
    if (listen(Fd, 1) != 0) {
        perror("listen");
        close(Fd);
        return -1;
    }
    return Fd;
}

static void StripCr(char *Line, size_t *Len) {
    if (*Len > 0 && Line[*Len - 1] == '\r') {
        Line[--(*Len)] = 0;
    }
}

/* 从 Fd 读到一行（含 \\n 剥掉）；超长丢弃到行尾。返回字节数，0=EOF，-1=错。 */
static int RecvLine(int Fd, char *Out, size_t Cap) {
    size_t N = 0;
    int Drop = 0;

    for (;;) {
        char C;
        ssize_t R = recv(Fd, &C, 1, 0);
        if (R == 0) {
            return 0;
        }
        if (R < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (C == '\n') {
            if (Drop) {
                N = 0;
                Drop = 0;
                continue; /* 超长行整行丢弃，等下一行 */
            }
            Out[N] = 0;
            StripCr(Out, &N);
            return (int)N;
        }
        if (Drop) {
            continue;
        }
        if (N + 1 >= Cap || N >= CHAT_LINE_MAX) {
            fprintf(stderr, "chatd: line too long, drop\n");
            Drop = 1;
            N = 0;
            continue;
        }
        Out[N++] = C;
    }
}

static int SendLine(int Fd, const char *Line, size_t Len) {
    char Buf[CHAT_LINE_MAX + 2];
    size_t I;
    size_t Off = 0;
    size_t Total;

    if (Len > CHAT_LINE_MAX) {
        fprintf(stderr, "chatd: stdin line too long, drop\n");
        return 0;
    }
    for (I = 0; I < Len; I++) {
        Buf[I] = Line[I];
    }
    Buf[Len] = '\n';
    Total = Len + 1;
    while (Off < Total) {
        ssize_t W = send(Fd, Buf + Off, Total - Off, 0);
        if (W < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        Off += (size_t)W;
    }
    return 0;
}

static int Session(int Client) {
    char Line[CHAT_LINE_MAX + 4];
    int On = 1;

    (void)setsockopt(Client, IPPROTO_TCP, TCP_NODELAY, &On, sizeof(On));
    fprintf(stderr, "chatd: client connected\n");
    fprintf(stderr, "chatd: 半双工 — 等 NUC 先发一行，再在本终端打字回车回复（无 chat>）\n");

    for (;;) {
        struct pollfd P[2];
        int Rc;

        P[0].fd = STDIN_FILENO;
        P[0].events = POLLIN;
        P[1].fd = Client;
        P[1].events = POLLIN;
        Rc = poll(P, 2, -1);
        if (Rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            return -1;
        }
        if (P[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "chatd: client gone\n");
            return 0;
        }
        if (P[1].revents & POLLIN) {
            int N = RecvLine(Client, Line, sizeof(Line));
            if (N < 0) {
                perror("recv");
                return -1;
            }
            if (N == 0) {
                fprintf(stderr, "chatd: client EOF\n");
                return 0;
            }
            /* 前缀区分：对端来的行 vs 本机 stdin 本地回显 */
            fprintf(stdout, "nuc> %.*s\n", N, Line);
            fflush(stdout);
            fprintf(stderr, "host> ");
            fflush(stderr);
        }
        if (P[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fprintf(stderr, "chatd: stdin closed\n");
            return 0;
        }
        if (P[0].revents & POLLIN) {
            if (!fgets(Line, (int)sizeof(Line), stdin)) {
                fprintf(stderr, "chatd: stdin EOF\n");
                return 0;
            }
            {
                size_t Len = strlen(Line);
                StripCr(Line, &Len);
                if (Len > 0 && Line[Len - 1] == '\n') {
                    Line[--Len] = 0;
                }
                if (Len == 0) {
                    fprintf(stderr, "chatd: 空行未发送（再打一行文字后回车）\n");
                    fprintf(stderr, "host> ");
                    fflush(stderr);
                    continue;
                }
                if (SendLine(Client, Line, Len) != 0) {
                    perror("send");
                    return -1;
                }
                fprintf(stderr, "chatd: sent %zu B → waiting next from NUC\n", Len);
            }
        }
    }
}

int main(int Argc, char **Argv) {
    unsigned Port = CHAT_PORT_DEFAULT;
    int Listen;
    int Client;
    struct sockaddr_in Peer;
    socklen_t PeerLen;

    if (Argc >= 2) {
        Port = (unsigned)strtoul(Argv[1], 0, 10);
        if (Port == 0 || Port > 65535u) {
            fprintf(stderr, "usage: %s [port]\n", Argv[0]);
            return 1;
        }
    }

    Listen = ListenPort(Port);
    if (Listen < 0) {
        return 1;
    }
    fprintf(stderr, "chatd: listen 0.0.0.0:%u (Ctrl+C stop)\n", Port);
    fprintf(stderr, "hint: nc 127.0.0.1 %u\n", Port);

    for (;;) {
        PeerLen = sizeof(Peer);
        Client = accept(Listen, (struct sockaddr *)&Peer, &PeerLen);
        if (Client < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            close(Listen);
            return 1;
        }
        {
            char Ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &Peer.sin_addr, Ip, sizeof(Ip));
            fprintf(stderr, "chatd: accept %s:%u\n", Ip, (unsigned)ntohs(Peer.sin_port));
        }
        (void)Session(Client);
        close(Client);
        fprintf(stderr, "chatd: waiting next client…\n");
    }
}
