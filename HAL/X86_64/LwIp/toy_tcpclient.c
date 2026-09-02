/*
 * toy_tcpclient.c — lwIP 主动 TCP 连接并发送（NO_SYS raw API）
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "toy_tcpclient.h"
#include "toy_ip.h"
#include "LwIp.h"
#include "Hal.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

#define TOY_TCP_SEND_MAX 512

typedef struct {
    struct tcp_pcb *Pcb;
    volatile int Phase; /* 0 waiting, 1 connected, 2 done, -1 error */
    volatile err_t Err;
    int FailCode;
    const void *Data;
    UINTN DataLen;
} CLIENT_CTX;

static CLIENT_CTX gClient;

static void ClientCleanup(void) {
    if (gClient.Pcb != NULL) {
        tcp_arg(gClient.Pcb, NULL);
        tcp_recv(gClient.Pcb, NULL);
        tcp_sent(gClient.Pcb, NULL);
        tcp_err(gClient.Pcb, NULL);
        tcp_poll(gClient.Pcb, NULL, 0);
        if (gClient.Phase >= 0 && gClient.Phase < 2) {
            tcp_abort(gClient.Pcb);
        }
        gClient.Pcb = NULL;
    }
}

static void ClientFail(err_t Err) {
    gClient.Err = Err;
    gClient.FailCode = (gClient.Phase == 1) ? -3 : -1;
    gClient.Phase = -1;
    if (gClient.Pcb != NULL) {
        tcp_abort(gClient.Pcb);
        gClient.Pcb = NULL;
    }
}

static err_t ClientRecv(void *Arg, struct tcp_pcb *Pcb, struct pbuf *P, err_t Err) {
    (void)Arg;
    if (Err != ERR_OK) {
        ClientFail(Err);
        return ERR_OK;
    }
    if (P == NULL) {
        tcp_close(Pcb);
        gClient.Pcb = NULL;
        gClient.Phase = 2;
        return ERR_OK;
    }
    tcp_recved(Pcb, P->tot_len);
    pbuf_free(P);
    return ERR_OK;
}

static err_t ClientSent(void *Arg, struct tcp_pcb *Pcb, u16_t Len) {
    (void)Arg;
    (void)Len;
    tcp_close(Pcb);
    gClient.Pcb = NULL;
    gClient.Phase = 2;
    return ERR_OK;
}

static void ClientError(void *Arg, err_t Err) {
    (void)Arg;
    gClient.Pcb = NULL;
    ClientFail(Err);
}

static err_t ClientConnected(void *Arg, struct tcp_pcb *Pcb, err_t Err) {
    err_t WrErr;

    (void)Arg;
    if (Err != ERR_OK) {
        ClientFail(Err);
        return Err;
    }
    gClient.Pcb = Pcb;
    gClient.Phase = 1;
    tcp_recv(Pcb, ClientRecv);
    tcp_sent(Pcb, ClientSent);
    if (gClient.DataLen == 0) {
        tcp_close(Pcb);
        gClient.Pcb = NULL;
        gClient.Phase = 2;
        return ERR_OK;
    }
    WrErr = tcp_write(Pcb, gClient.Data, (u16_t)gClient.DataLen, TCP_WRITE_FLAG_COPY);
    if (WrErr != ERR_OK) {
        ClientFail(WrErr);
        return WrErr;
    }
    WrErr = tcp_output(Pcb);
    if (WrErr != ERR_OK) {
        ClientFail(WrErr);
        return WrErr;
    }
    return ERR_OK;
}

int LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                       const void *Data, UINTN Len, int TimeoutMs) {
    struct tcp_pcb *Pcb;
    ip4_addr_t Remote;
    err_t Err;
    int Tries;

    if (!LwIpActive() || DstPort == 0) {
        return -1;
    }
    if (Data == NULL && Len > 0) {
        return -1;
    }
    if (Len > TOY_TCP_SEND_MAX) {
        return -3;
    }
    if (gClient.Pcb != NULL || gClient.Phase != 0) {
        ClientCleanup();
        gClient.Phase = 0;
        gClient.FailCode = 0;
    }

    gClient.Pcb = NULL;
    gClient.Phase = 0;
    gClient.Err = ERR_OK;
    gClient.FailCode = 0;
    gClient.Data = Data;
    gClient.DataLen = Len;

    Pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (Pcb == NULL) {
        return -1;
    }
    tcp_arg(Pcb, &gClient);
    tcp_err(Pcb, ClientError);

    ToyHostIpToLwIp(DstIp, &Remote);
    Err = tcp_connect(Pcb, ip_2_ip4(&Remote), DstPort, ClientConnected);
    if (Err != ERR_OK) {
        tcp_abort(Pcb);
        gClient.Pcb = NULL;
        return -1;
    }
    gClient.Pcb = Pcb;

    Tries = TimeoutMs > 0 ? TimeoutMs : 3000;
    while (Tries-- > 0) {
        if (gClient.Phase == 2) {
            gClient.Phase = 0;
            return 0;
        }
        if (gClient.Phase < 0) {
            int Ret = gClient.FailCode != 0 ? gClient.FailCode : -1;
            gClient.Phase = 0;
            gClient.FailCode = 0;
            return Ret;
        }
        HalNetPoll();
        LwIpPoll();
        HalCpuHalt();
    }

    ClientCleanup();
    gClient.Phase = 0;
    return -2;
}

#else

int LwIpTcpConnectSend(UINT32 DstIp, UINT16 DstPort,
                       const void *Data, UINTN Len, int TimeoutMs) {
    (void)DstIp;
    (void)DstPort;
    (void)Data;
    (void)Len;
    (void)TimeoutMs;
    return -1;
}

#endif
