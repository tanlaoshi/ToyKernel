/*
 * toy_socket.c — lwIP 持久 TCP socket（NO_SYS raw API）
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "toy_socket.h"
#include "toy_ip.h"
#include "Hal.h"
#include "LwIp.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#define SOCK_RX_MAX 2048

typedef struct {
    int Used;
    struct tcp_pcb *Pcb;
    volatile int Phase; /* 0 connecting, 1 connected, 2 closed, -1 error */
    volatile err_t Err;
    UINT8 Rx[SOCK_RX_MAX];
    UINTN RxLen;
} TOY_SOCK;

static TOY_SOCK gSocks[TOY_SOCK_MAX];

static TOY_SOCK *SockGet(int Sock) {
    if (Sock < 0 || Sock >= TOY_SOCK_MAX || !gSocks[Sock].Used) {
        return 0;
    }
    return &gSocks[Sock];
}

static void SockAbortPcb(TOY_SOCK *S) {
    if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_sent(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        tcp_poll(S->Pcb, NULL, 0);
        tcp_abort(S->Pcb);
        S->Pcb = NULL;
    }
}

static err_t SockRecvCb(void *Arg, struct tcp_pcb *Pcb, struct pbuf *P, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;
    UINTN Space;
    UINTN Copy;

    (void)Pcb;
    if (S == NULL) {
        if (P != NULL) {
            pbuf_free(P);
        }
        return ERR_OK;
    }
    if (Err != ERR_OK) {
        S->Err = Err;
        S->Phase = -1;
        return ERR_OK;
    }
    if (P == NULL) {
        S->Phase = 2;
        S->Pcb = NULL;
        return ERR_OK;
    }
    Space = SOCK_RX_MAX - S->RxLen;
    Copy = P->tot_len;
    if (Copy > Space) {
        Copy = Space;
    }
    if (Copy > 0) {
        pbuf_copy_partial(P, S->Rx + S->RxLen, (u16_t)Copy, 0);
        S->RxLen += Copy;
        tcp_recved(Pcb, (u16_t)Copy);
    }
    /* 丢弃超出缓冲部分，避免卡死对端窗口 */
    if (P->tot_len > Copy) {
        tcp_recved(Pcb, (u16_t)(P->tot_len - Copy));
    }
    pbuf_free(P);
    return ERR_OK;
}

static void SockErrCb(void *Arg, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;

    if (S == NULL) {
        return;
    }
    S->Pcb = NULL;
    S->Err = Err;
    S->Phase = -1;
}

static err_t SockConnectedCb(void *Arg, struct tcp_pcb *Pcb, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;

    if (S == NULL) {
        return Err;
    }
    if (Err != ERR_OK) {
        S->Err = Err;
        S->Phase = -1;
        S->Pcb = NULL;
        return Err;
    }
    S->Pcb = Pcb;
    S->Phase = 1;
    tcp_recv(Pcb, SockRecvCb);
    tcp_err(Pcb, SockErrCb);
    return ERR_OK;
}

int ToySocketCreate(void) {
    int i;

    for (i = 0; i < TOY_SOCK_MAX; i++) {
        if (!gSocks[i].Used) {
            gSocks[i].Used = 1;
            gSocks[i].Pcb = NULL;
            gSocks[i].Phase = 0;
            gSocks[i].Err = ERR_OK;
            gSocks[i].RxLen = 0;
            return i;
        }
    }
    return -1;
}

int ToySocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    ip4_addr_t Remote;
    err_t Err;
    int Tries;

    if (S == NULL || S->Pcb != NULL || S->Phase == 1) {
        return -1;
    }
    Pcb = tcp_new();
    if (Pcb == NULL) {
        return -1;
    }
    S->Phase = 0;
    S->Err = ERR_OK;
    S->Pcb = Pcb;
    tcp_arg(Pcb, S);
    tcp_err(Pcb, SockErrCb);
    ToyHostIpToLwIp(DstIp, &Remote);
    Err = tcp_connect(Pcb, &Remote, DstPort, SockConnectedCb);
    if (Err != ERR_OK) {
        SockAbortPcb(S);
        S->Phase = -1;
        return -1;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 3000;
    while (S->Phase == 0 && Tries-- > 0) {
        HalNetPoll();
        LwIpPoll();
    }
    if (S->Phase != 1) {
        SockAbortPcb(S);
        S->Phase = -1;
        return -1;
    }
    return 0;
}

int ToySocketSend(int Sock, const void *Data, UINTN Len) {
    TOY_SOCK *S = SockGet(Sock);
    err_t Err;
    UINTN Sent = 0;
    int Tries = 2000;

    if (S == NULL || S->Phase != 1 || S->Pcb == NULL || Data == NULL) {
        return -1;
    }
    while (Sent < Len && Tries-- > 0) {
        UINTN Chunk = Len - Sent;
        u16_t Avail = tcp_sndbuf(S->Pcb);

        if (Avail == 0) {
            HalNetPoll();
            LwIpPoll();
            continue;
        }
        if (Chunk > Avail) {
            Chunk = Avail;
        }
        if (Chunk > 512) {
            Chunk = 512;
        }
        Err = tcp_write(S->Pcb, (const UINT8 *)Data + Sent, (u16_t)Chunk,
                        TCP_WRITE_FLAG_COPY);
        if (Err == ERR_MEM) {
            HalNetPoll();
            LwIpPoll();
            continue;
        }
        if (Err != ERR_OK) {
            return Sent > 0 ? (int)Sent : -1;
        }
        (void)tcp_output(S->Pcb);
        Sent += Chunk;
        HalNetPoll();
        LwIpPoll();
        if (S->Phase != 1) {
            break;
        }
    }
    return Sent > 0 ? (int)Sent : -1;
}

int ToySocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    UINTN N;
    UINTN i;
    int Tries;

    if (S == NULL || Buf == NULL || Len == 0) {
        return -1;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 1;
    while (S->RxLen == 0 && S->Phase == 1 && Tries-- > 0) {
        HalNetPoll();
        LwIpPoll();
    }
    if (S->RxLen == 0) {
        if (S->Phase == 2) {
            return -2;
        }
        if (S->Phase < 0) {
            return -1;
        }
        return 0;
    }
    N = S->RxLen;
    if (N > Len) {
        N = Len;
    }
    for (i = 0; i < N; i++) {
        ((UINT8 *)Buf)[i] = S->Rx[i];
    }
    for (i = N; i < S->RxLen; i++) {
        S->Rx[i - N] = S->Rx[i];
    }
    S->RxLen -= N;
    return (int)N;
}

int ToySocketClose(int Sock) {
    TOY_SOCK *S = SockGet(Sock);

    if (S == NULL) {
        return -1;
    }
    if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        if (tcp_close(S->Pcb) != ERR_OK) {
            tcp_abort(S->Pcb);
        }
        S->Pcb = NULL;
    }
    S->Used = 0;
    S->Phase = 0;
    S->RxLen = 0;
    return 0;
}

#else

#include "toy_socket.h"

int ToySocketCreate(void) {
    return -1;
}

int ToySocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    (void)Sock;
    (void)DstIp;
    (void)DstPort;
    (void)TimeoutMs;
    return -1;
}

int ToySocketSend(int Sock, const void *Data, UINTN Len) {
    (void)Sock;
    (void)Data;
    (void)Len;
    return -1;
}

int ToySocketRecv(int Sock, void *Buf, UINTN Len, int TimeoutMs) {
    (void)Sock;
    (void)Buf;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

int ToySocketClose(int Sock) {
    (void)Sock;
    return -1;
}

#endif
