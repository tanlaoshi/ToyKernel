/*
 * toy_tcpecho.c — lwIP TCP echo（改编自 contrib/apps/tcpecho_raw）
 */
#include "lwip/opt.h"

#if LWIP_TCP

#include "toy_tcpecho.h"
#include "Console.h"
#include "lwip/tcp.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"

static struct tcp_pcb *gListenPcb;
static UINT16 gListenPort;

enum echo_state {
    ES_ACCEPTED = 0,
    ES_RECEIVED,
    ES_CLOSING
};

struct echo_conn {
    u8_t state;
    struct tcp_pcb *pcb;
    struct pbuf *p;
};

static void EchoFree(struct echo_conn *Es) {
    if (Es == NULL) {
        return;
    }
    if (Es->p != NULL) {
        pbuf_free(Es->p);
    }
    mem_free(Es);
}

static void EchoClose(struct tcp_pcb *Tpcb, struct echo_conn *Es) {
    tcp_arg(Tpcb, NULL);
    tcp_sent(Tpcb, NULL);
    tcp_recv(Tpcb, NULL);
    tcp_err(Tpcb, NULL);
    tcp_poll(Tpcb, NULL, 0);
    EchoFree(Es);
    tcp_close(Tpcb);
}

static void EchoSend(struct tcp_pcb *Tpcb, struct echo_conn *Es) {
    struct pbuf *Ptr;
    err_t WrErr = ERR_OK;

    while (WrErr == ERR_OK && Es->p != NULL && Es->p->len <= tcp_sndbuf(Tpcb)) {
        Ptr = Es->p;
        WrErr = tcp_write(Tpcb, Ptr->payload, Ptr->len, TCP_WRITE_FLAG_COPY);
        if (WrErr == ERR_OK) {
            u16_t Plen = Ptr->len;
            Es->p = Ptr->next;
            if (Es->p != NULL) {
                pbuf_ref(Es->p);
            }
            pbuf_free(Ptr);
            tcp_recved(Tpcb, Plen);
        } else if (WrErr == ERR_MEM) {
            Es->p = Ptr;
        }
    }
}

static void EchoError(void *Arg, err_t Err) {
    (void)Err;
    EchoFree((struct echo_conn *)Arg);
}

static err_t EchoPoll(void *Arg, struct tcp_pcb *Tpcb) {
    struct echo_conn *Es = (struct echo_conn *)Arg;

    if (Es == NULL) {
        tcp_abort(Tpcb);
        return ERR_ABRT;
    }
    if (Es->p != NULL) {
        EchoSend(Tpcb, Es);
    } else if (Es->state == ES_CLOSING) {
        EchoClose(Tpcb, Es);
    }
    return ERR_OK;
}

static err_t EchoSent(void *Arg, struct tcp_pcb *Tpcb, u16_t Len) {
    struct echo_conn *Es = (struct echo_conn *)Arg;

    (void)Len;
    if (Es == NULL) {
        return ERR_OK;
    }
    if (Es->p != NULL) {
        tcp_sent(Tpcb, EchoSent);
        EchoSend(Tpcb, Es);
    } else if (Es->state == ES_CLOSING) {
        EchoClose(Tpcb, Es);
    }
    return ERR_OK;
}

static void EchoLogData(struct pbuf *P) {
    struct pbuf *Q;
    char Buf[96];
    int Pos = 0;
    UINTN i;
    UINTN Total = 0;
    const char *Prefix = "lwip echo: ";

    while (Prefix[Pos] && Pos < (int)sizeof(Buf) - 2) {
        Buf[Pos] = Prefix[Pos];
        Pos++;
    }
    for (Q = P; Q != NULL; Q = Q->next) {
        const u8_t *Bytes = (const u8_t *)Q->payload;
        for (i = 0; i < Q->len && Total < 64 && Pos < (int)sizeof(Buf) - 2; i++, Total++) {
            char C = (char)Bytes[i];
            if (C >= 32 && C <= 126) {
                Buf[Pos++] = C;
            }
        }
    }
    Buf[Pos++] = '\n';
    Buf[Pos] = 0;
    ConsoleNotify(Buf);
}

static err_t EchoRecv(void *Arg, struct tcp_pcb *Tpcb, struct pbuf *P, err_t Err) {
    struct echo_conn *Es = (struct echo_conn *)Arg;

    if (Es == NULL) {
        if (P != NULL) {
            pbuf_free(P);
        }
        return ERR_ARG;
    }
    if (P == NULL) {
        Es->state = ES_CLOSING;
        if (Es->p == NULL) {
            EchoClose(Tpcb, Es);
        } else {
            EchoSend(Tpcb, Es);
        }
        return ERR_OK;
    }
    if (Err != ERR_OK) {
        return Err;
    }
    EchoLogData(P);
    if (Es->state == ES_ACCEPTED) {
        Es->state = ES_RECEIVED;
        Es->p = P;
        EchoSend(Tpcb, Es);
        return ERR_OK;
    }
    if (Es->state == ES_RECEIVED) {
        if (Es->p == NULL) {
            Es->p = P;
            EchoSend(Tpcb, Es);
        } else {
            pbuf_cat(Es->p, P);
        }
        return ERR_OK;
    }
    tcp_recved(Tpcb, P->tot_len);
    pbuf_free(P);
    return ERR_OK;
}

static err_t EchoAccept(void *Arg, struct tcp_pcb *NewPcb, err_t Err) {
    struct echo_conn *Es;

    (void)Arg;
    if (Err != ERR_OK || NewPcb == NULL) {
        return ERR_VAL;
    }
    Es = (struct echo_conn *)mem_malloc(sizeof(struct echo_conn));
    if (Es == NULL) {
        return ERR_MEM;
    }
    Es->state = ES_ACCEPTED;
    Es->pcb = NewPcb;
    Es->p = NULL;
    tcp_setprio(NewPcb, TCP_PRIO_MIN);
    tcp_arg(NewPcb, Es);
    tcp_recv(NewPcb, EchoRecv);
    tcp_err(NewPcb, EchoError);
    tcp_poll(NewPcb, EchoPoll, 0);
    tcp_sent(NewPcb, EchoSent);
    ConsoleNotify("lwip: client connected\n");
    return ERR_OK;
}

UINT16 LwIpTcpListenPort(void) {
    return gListenPort;
}

int LwIpTcpListen(UINT16 Port) {
    err_t Err;

    if (gListenPcb != NULL) {
        return -1;
    }
    gListenPcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (gListenPcb == NULL) {
        return -1;
    }
    Err = tcp_bind(gListenPcb, IP_ANY_TYPE, Port);
    if (Err != ERR_OK) {
        tcp_abort(gListenPcb);
        gListenPcb = NULL;
        return -1;
    }
    gListenPcb = tcp_listen(gListenPcb);
    if (gListenPcb == NULL) {
        return -1;
    }
    tcp_accept(gListenPcb, EchoAccept);
    gListenPort = Port;
    return 0;
}

int LwIpTcpListenStop(void) {
    if (gListenPcb == NULL) {
        return -1;
    }
    tcp_close(gListenPcb);
    gListenPcb = NULL;
    gListenPort = 0;
    return 0;
}

#else

UINT16 LwIpTcpListenPort(void) {
    return 0;
}

int LwIpTcpListen(UINT16 Port) {
    (void)Port;
    return -1;
}

int LwIpTcpListenStop(void) {
    return -1;
}

#endif
