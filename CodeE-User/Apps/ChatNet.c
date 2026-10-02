/* ChatNet.c — peer=TOYOS.DB + 行发送 */
#include "ChatNet.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <ToyNet.h>

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

int ChatLoadPeer(unsigned *OutIp, unsigned short *OutPort) {
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
            printf("chat: bad chat.peer=%s\n", P);
            return -1;
        }
        return 0;
    }
    close(Fd);
    printf("chat: missing chat.peer\n");
    return -1;
}

int ChatSendLine(int Fd, const char *Line, size_t Len) {
    char Buf[CHAT_LINE_MAX + 2];
    size_t Off = 0;
    size_t I;

    if (Len > CHAT_LINE_MAX) {
        return 0;
    }
    for (I = 0; I < Len; I++) {
        Buf[I] = Line[I];
    }
    Buf[Len] = '\n';
    while (Off < Len + 1) {
        ssize_t W = send(Fd, Buf + Off, Len + 1 - Off, 0);
        if (W <= 0) {
            return -1;
        }
        Off += (size_t)W;
    }
    return 0;
}
