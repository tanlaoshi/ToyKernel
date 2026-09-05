/*
 * toy_socket.c — lwIP 持久 TCP socket（NO_SYS raw API）
 * 支持 connect 客户端与 bind/listen/accept 服务端。
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

/* Phase: 0 idle/connecting, 1 connected, 2 peer-closed, 3 bound, 4 listening, -1 error */
typedef struct {
    int Used;
    struct tcp_pcb *Pcb;
    volatile int Phase;
    volatile err_t Err;
    UINT8 Rx[SOCK_RX_MAX];
    UINTN RxLen;
    int Pending[TOY_SOCK_PENDING];
    int PendingCount;
    int IsListen;
} TOY_SOCK;

static TOY_SOCK gSocks[TOY_SOCK_MAX];

static TOY_SOCK *SockGet(int Sock) {
    if (Sock < 0 || Sock >= TOY_SOCK_MAX || !gSocks[Sock].Used) {
        return 0;
    }
    return &gSocks[Sock];
}

static int SockAllocSlot(void) {
    int i;

    for (i = 0; i < TOY_SOCK_MAX; i++) {
        if (!gSocks[i].Used) {
            gSocks[i].Used = 1;
            gSocks[i].Pcb = NULL;
            gSocks[i].Phase = 0;
            gSocks[i].Err = ERR_OK;
            gSocks[i].RxLen = 0;
            gSocks[i].PendingCount = 0;
            gSocks[i].IsListen = 0;
            return i;
        }
    }
    return -1;
}

static void SockAbortPcb(TOY_SOCK *S) {
    if (S->Pcb != NULL) {
        tcp_arg(S->Pcb, NULL);
        tcp_recv(S->Pcb, NULL);
        tcp_sent(S->Pcb, NULL);
        tcp_err(S->Pcb, NULL);
        tcp_poll(S->Pcb, NULL, 0);
        tcp_accept(S->Pcb, NULL);
        tcp_abort(S->Pcb);
        S->Pcb = NULL;
    }
}

static void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb);

static err_t SockRecvCb(void *Arg, struct tcp_pcb *Pcb, struct pbuf *P, err_t Err) {
    TOY_SOCK *S = (TOY_SOCK *)Arg;
    UINTN Space;
    UINTN Copy;

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
    SockSetupConnected(S, Pcb);
    return ERR_OK;
}

static void SockSetupConnected(TOY_SOCK *S, struct tcp_pcb *Pcb) {
    S->Pcb = Pcb;
    S->Phase = 1;
    S->IsListen = 0;
    S->RxLen = 0;
    tcp_arg(Pcb, S);
    tcp_recv(Pcb, SockRecvCb);
    tcp_err(Pcb, SockErrCb);
}

static err_t SockAcceptCb(void *Arg, struct tcp_pcb *NewPcb, err_t Err) {
    TOY_SOCK *Listen = (TOY_SOCK *)Arg;
    int Child;
    TOY_SOCK *Cs;

    if (Listen == NULL || Err != ERR_OK || NewPcb == NULL) {
        if (NewPcb != NULL) {
            tcp_abort(NewPcb);
        }
        return ERR_VAL;
    }
    if (Listen->PendingCount >= TOY_SOCK_PENDING) {
        tcp_abort(NewPcb);
        return ERR_MEM;
    }
    Child = SockAllocSlot();
    if (Child < 0) {
        tcp_abort(NewPcb);
        return ERR_MEM;
    }
    Cs = &gSocks[Child];
    tcp_setprio(NewPcb, TCP_PRIO_MIN);
    SockSetupConnected(Cs, NewPcb);
    Listen->Pending[Listen->PendingCount++] = Child;
    return ERR_OK;
}

int ToySocketCreate(void) {
    return SockAllocSlot();
}

int ToySocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    err_t Err;

    if (S == NULL || S->Phase == 1 || S->Phase == 4 || Port == 0) {
        return -1;
    }
    if (S->Pcb == NULL) {
        Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (Pcb == NULL) {
            return -1;
        }
        S->Pcb = Pcb;
    } else {
        Pcb = S->Pcb;
    }
    if (Ip == 0) {
        Err = tcp_bind(Pcb, IP_ANY_TYPE, Port);
    } else {
        ip4_addr_t A4;
        ip_addr_t Addr;

        ToyHostIpToLwIp(Ip, &A4);
        ip_addr_copy_from_ip4(Addr, A4);
        Err = tcp_bind(Pcb, &Addr, Port);
    }
    if (Err != ERR_OK) {
        return -1;
    }
    S->Phase = 3;
    return 0;
}

int ToySocketListen(int Sock, int Backlog) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *ListenPcb;

    (void)Backlog;
    if (S == NULL || S->Pcb == NULL || S->Phase != 3) {
        return -1;
    }
    ListenPcb = tcp_listen_with_backlog(S->Pcb, TOY_SOCK_PENDING);
    if (ListenPcb == NULL) {
        return -1;
    }
    S->Pcb = ListenPcb;
    S->IsListen = 1;
    S->PendingCount = 0;
    S->Phase = 4;
    tcp_arg(ListenPcb, S);
    tcp_accept(ListenPcb, SockAcceptCb);
    return 0;
}

int ToySocketAccept(int Sock, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    int Child;
    int Tries;
    int i;
    int Forever;

    if (S == NULL || S->Phase != 4) {
        return -1;
    }
    Forever = (TimeoutMs <= 0);
    Tries = Forever ? 1 : TimeoutMs;
    while (S->PendingCount == 0 && S->Phase == 4) {
        LwIpService();
        HalCpuHalt();
        if (!Forever) {
            if (--Tries <= 0) {
                break;
            }
        }
    }
    if (S->PendingCount == 0) {
        return -1;
    }
    Child = S->Pending[0];
    for (i = 1; i < S->PendingCount; i++) {
        S->Pending[i - 1] = S->Pending[i];
    }
    S->PendingCount--;
    return Child;
}

int ToySocketConnect(int Sock, UINT32 DstIp, UINT16 DstPort, int TimeoutMs) {
    TOY_SOCK *S = SockGet(Sock);
    struct tcp_pcb *Pcb;
    ip4_addr_t Remote;
    err_t Err;
    int Tries;

    if (S == NULL || S->Phase == 1 || S->Phase == 4 || S->IsListen) {
        return -1;
    }
    {
        UINT64 IrqFlags = HalIrqSave();

        if (S->Pcb == NULL) {
            Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
            if (Pcb == NULL) {
                HalIrqRestore(IrqFlags);
                return -1;
            }
            S->Pcb = Pcb;
        } else {
            Pcb = S->Pcb;
        }
        S->Phase = 0;
        S->Err = ERR_OK;
        tcp_arg(Pcb, S);
        tcp_err(Pcb, SockErrCb);
        ToyHostIpToLwIp(DstIp, &Remote);
        Err = tcp_connect(Pcb, ip_2_ip4(&Remote), DstPort, SockConnectedCb);
        HalIrqRestore(IrqFlags);
    }
    if (Err != ERR_OK) {
        SockAbortPcb(S);
        S->Phase = -1;
        return -1;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 8000;
    while (S->Phase == 0 && Tries-- > 0) {
        LwIpService();
        HalCpuHalt(); /* 等定时器/网卡；无 halt 会把 TimeoutMs 当空转次数瞬间耗尽 */
    }
    if (S->Phase != 1) {
        HalDebugWrite("sock: connect fail phase=");
        HalDebugHex32((UINT32)S->Phase);
        HalDebugWrite(" err=");
        HalDebugHex32((UINT32)(INT32)S->Err);
        if ((INT32)S->Err == -14) {
            HalDebugWrite(" (RST: host nc -l -p PORT first?)\n");
        } else {
            HalDebugWrite("\n");
        }
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
            LwIpService();
            HalCpuHalt();
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
            LwIpService();
            HalCpuHalt();
            continue;
        }
        if (Err != ERR_OK) {
            return Sent > 0 ? (int)Sent : -1;
        }
        (void)tcp_output(S->Pcb);
        Sent += Chunk;
        LwIpService();
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
        LwIpService();
        HalCpuHalt();
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
    int Pending[TOY_SOCK_PENDING];
    int Count;
    int i;

    if (S == NULL) {
        return -1;
    }
    if (S->IsListen) {
        Count = S->PendingCount;
        for (i = 0; i < Count; i++) {
            Pending[i] = S->Pending[i];
        }
        S->PendingCount = 0;
        for (i = 0; i < Count; i++) {
            ToySocketClose(Pending[i]);
        }
        if (S->Pcb != NULL) {
            tcp_arg(S->Pcb, NULL);
            tcp_accept(S->Pcb, NULL);
            tcp_close(S->Pcb);
            S->Pcb = NULL;
        }
    } else if (S->Pcb != NULL) {
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
    S->IsListen = 0;
    return 0;
}

#else

#include "toy_socket.h"

int ToySocketCreate(void) {
    return -1;
}

int ToySocketBind(int Sock, UINT32 Ip, UINT16 Port) {
    (void)Sock;
    (void)Ip;
    (void)Port;
    return -1;
}

int ToySocketListen(int Sock, int Backlog) {
    (void)Sock;
    (void)Backlog;
    return -1;
}

int ToySocketAccept(int Sock, int TimeoutMs) {
    (void)Sock;
    (void)TimeoutMs;
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
