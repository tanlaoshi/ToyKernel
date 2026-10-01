/* Chat.c — 串口行聊天客户端（PR-CHAT-2）
 * peer=TOYOS.DB chat.peer=；半双工（fork 不克隆 socket，见 TaskCloneFds）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ToyNet.h>

#define CHAT_LINE_MAX 200
#define CHAT_PORT_DEF 9090

static int ReadDec(const char **Pp, unsigned *Out) {
    const char *P = *Pp;
    unsigned V = 0;
    int Dig = 0;

    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (unsigned)(*P - '0');
        P++;
        Dig = 1;
        if (V > 65535u) {
            return -1;
        }
    }
    if (!Dig) {
        return -1;
    }
    *Out = V;
    *Pp = P;
    return 0;
}

static int ParsePeer(const char *Val, unsigned *OutIp, unsigned short *OutPort) {
    const char *P = Val;
    unsigned A, B, C, D, Port = CHAT_PORT_DEF;

    if (!Val || !OutIp || !OutPort) {
        return -1;
    }
    if (ReadDec(&P, &A) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &B) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &C) != 0 || *P++ != '.') {
        return -1;
    }
    if (ReadDec(&P, &D) != 0) {
        return -1;
    }
    if (*P == ':') {
        P++;
        if (ReadDec(&P, &Port) != 0) {
            return -1;
        }
    }
    while (*P == ' ' || *P == '\t') {
        P++;
    }
    if (*P != 0 || A > 255 || B > 255 || C > 255 || D > 255
        || Port == 0 || Port > 65535) {
        return -1;
    }
    *OutIp = ToyNetIpv4(A, B, C, D);
    *OutPort = (unsigned short)Port;
    return 0;
}

static int ReadFileLine(int Fd, char *Out, size_t Cap) {
    size_t N = 0;

    for (;;) {
        char C;
        ssize_t R = read(Fd, &C, 1);
        if (R == 0) {
            if (N == 0) {
                return 0;
            }
            Out[N] = 0;
            return (int)N;
        }
        if (R < 0) {
            return -1;
        }
        if (C == '\n') {
            Out[N] = 0;
            return (int)N;
        }
        if (C == '\r') {
            continue;
        }
        if (N + 1 < Cap) {
            Out[N++] = C;
        }
    }
}

static int LoadChatPeer(unsigned *OutIp, unsigned short *OutPort) {
    int Fd;
    char Line[128];
    const char *Key = "chat.peer=";
    size_t KeyLen = 10;

    Fd = open("TOYOS.DB", O_RDONLY);
    if (Fd < 0) {
        Fd = open("/TOYOS.DB", O_RDONLY);
    }
    if (Fd < 0) {
        printf("chat: no TOYOS.DB (dbset chat.peer <ip>)\n");
        return -1;
    }
    while (ReadFileLine(Fd, Line, sizeof(Line)) > 0) {
        char *P = Line;
        while (*P == ' ' || *P == '\t') {
            P++;
        }
        if (strncmp(P, Key, KeyLen) != 0) {
            continue;
        }
        P += KeyLen;
        while (*P == ' ' || *P == '\t') {
            P++;
        }
        close(Fd);
        if (ParsePeer(P, OutIp, OutPort) != 0) {
            printf("chat: bad chat.peer=%s (want A.B.C.D[:9090])\n", P);
            return -1;
        }
        return 0;
    }
    close(Fd);
    printf("chat: missing chat.peer in TOYOS.DB\n");
    return -1;
}

static int RecvLine(int Fd, char *Out, size_t Cap) {
    size_t N = 0;
    int Drop = 0;

    for (;;) {
        char C;
        ssize_t R = recv(Fd, &C, 1, 0);
        if (R == 0) {
            return -2; /* 对端关连接；勿与空行 \\n（返回 0）混淆 */
        }
        if (R < 0) {
            return -1;
        }
        if (C == '\n') {
            if (Drop) {
                N = 0;
                Drop = 0;
                continue;
            }
            Out[N] = 0;
            if (N > 0 && Out[N - 1] == '\r') {
                Out[--N] = 0;
            }
            return (int)N; /* 可为 0：对端空回车 */
        }
        if (Drop) {
            continue;
        }
        if (N + 1 >= Cap || N >= CHAT_LINE_MAX) {
            printf("chat: line too long, drop\n");
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
        printf("chat: line too long, drop\n");
        return 0;
    }
    for (I = 0; I < Len; I++) {
        Buf[I] = Line[I];
    }
    Buf[Len] = '\n';
    Total = Len + 1;
    while (Off < Total) {
        ssize_t W = send(Fd, Buf + Off, Total - Off, 0);
        if (W <= 0) {
            return -1;
        }
        Off += (size_t)W;
    }
    return 0;
}

static int ReadStdinLine(char *Out, size_t Cap) {
    size_t N = 0;

    for (;;) {
        char C;
        ssize_t R = read(0, &C, 1);
        if (R == 0) {
            return N > 0 ? (int)N : 0;
        }
        if (R < 0) {
            return -1;
        }
        if (C == 4) {
            /* Ctrl+D：有缓冲则先交行，否则 EOF */
            Out[N] = 0;
            return N > 0 ? (int)N : 0;
        }
        /* 内核已把 CR→LF；再遇 CR 也当行尾（旧核/旁路） */
        if (C == '\n' || C == '\r') {
            Out[N] = 0;
            /* 空行：-2（重提示）；勿与 EOF(0) 混淆 */
            return N == 0 ? -2 : (int)N;
        }
        if (N + 1 >= Cap || N >= CHAT_LINE_MAX) {
            printf("chat: line too long, drop\n");
            while (C != '\n' && C != '\r') {
                if (read(0, &C, 1) <= 0) {
                    return -1;
                }
            }
            N = 0;
            continue;
        }
        Out[N++] = C;
    }
}

int main(void) {
    unsigned Ip;
    unsigned short Port;
    int Fd;
    char Line[CHAT_LINE_MAX + 4];

    if (LoadChatPeer(&Ip, &Port) != 0) {
        return 1;
    }
    printf("chat: peer %u.%u.%u.%u:%u\n",
           (Ip >> 24) & 0xffu, (Ip >> 16) & 0xffu,
           (Ip >> 8) & 0xffu, Ip & 0xffu, (unsigned)Port);

    Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) {
        printf("chat: socket fail (lwip on?)\n");
        return 1;
    }
    printf("chat: connecting…\n");
    if (ToyNetConnect(Fd, Ip, Port) != 0) {
        printf("chat: connect fail\n");
        close(Fd);
        return 1;
    }
    printf("chat: connected (half-duplex: you send, then wait peer)\n");

    for (;;) {
        int N;
        printf("chat> ");
        N = ReadStdinLine(Line, sizeof(Line));
        if (N == -2) {
            continue; /* 空回车 */
        }
        if (N < 0) {
            break;
        }
        if (N == 0) {
            printf("chat: bye\n");
            break;
        }
        if (strcmp(Line, "/quit") == 0) {
            printf("chat: bye\n");
            break;
        }
        if (SendLine(Fd, Line, (size_t)N) != 0) {
            printf("chat: send fail\n");
            break;
        }
        /* 半双工：必须收到对端一行才回到 chat>。空行忽略（宿主误按回车勿脱步） */
        for (;;) {
            N = RecvLine(Fd, Line, sizeof(Line));
            if (N == -2) {
                printf("chat: peer closed\n");
                goto Done;
            }
            if (N < 0) {
                printf("chat: recv fail\n");
                goto Done;
            }
            if (N == 0) {
                continue;
            }
            printf("%s\n", Line);
            break;
        }
    }
Done:
    close(Fd);
    return 0;
}
