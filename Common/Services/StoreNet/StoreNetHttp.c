/*
 * StoreNetHttp.c — HTTP/1.0 GET（内建 TCP）
 * 成功 0；正文在堆页内，调用方 FreePages。
 * 核心：StoreNet.c
 */
#include "StoreNetPrivate.h"
#include "Scheduler.h"
#include "LwIp.h"
#include "Console.h"

static char gHttpReq[256]; /* 避免 HttpGet 再占任务栈 */

static void PollNet(void) {
    HalNetPoll();
    TcpPoll();
}

static void HttpBreath(void) {
    SchedulerIoBreath();
    if (LwIpActive()) {
        LwIpService();
    } else {
        PollNet();
    }
}

static int WaitEstablished(int Tries) {
    int N = 0;

    while (Tries-- > 0) {
        PollNet();
        if ((++N & 0x3FF) == 0) {
            HttpBreath();
        }
        if (TcpGetState() == TCP_ESTABLISHED) {
            return 0;
        }
        if (TcpGetState() != TCP_SYN_SENT) {
            return STORE_ERR_NET;
        }
    }
    return STORE_ERR_NET;
}

int HttpGet(UINT32 Ip, UINT16 Port, const char *Path,
                   UINT8 **OutBody, UINTN *OutLen, UINT32 *OutPages) {
    int Rlen = 0;
    UINT8 *Resp;
    UINT32 Pages;
    UINTN Got = 0;
    UINTN Chunk;
    int Tries;
    int Rc;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    const char *P;

    if (!Path || !OutBody || !OutLen || !OutPages) {
        return STORE_ERR_NET;
    }
    *OutBody = 0;
    *OutLen = 0;
    *OutPages = 0;

    if (!HalNetReady()) {
        HalConsoleWriteSerial("store: net not ready\n");
        return STORE_ERR_NET;
    }

    Pages = (STORE_HTTP_MAX + 4095u) / 4096u;
    Resp = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Resp) {
        HalConsoleWriteSerial("store: alloc fail\n");
        return STORE_ERR_ALLOC;
    }

    /* lwip on：自研 Tcp 的 NetSendIp 恒失败；改走 LwIpSocket */
    if (LwIpActive()) {
        UINTN LwGot = 0;

        Rc = HttpGetLwIp(Ip, Port, Path, Resp, STORE_HTTP_MAX, &LwGot);
        if (Rc != 0) {
            PhysicalMemoryFreePages(Resp, Pages);
            return Rc;
        }
        Got = LwGot;
        goto ParseHttp;
    }

    if (TcpGetState() != TCP_CLOSED) {
        TcpClose();
    }
    if (TcpConnect(Ip, Port) != 0) {
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp syn fail\n");
        return STORE_ERR_NET;
    }
    if (WaitEstablished(STORE_HTTP_TRIES) != 0) {
        TcpClose();
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp connect timeout\n");
        return STORE_ERR_NET;
    }

    {
        const char *A = "GET ";
        const char *B = " HTTP/1.0\r\nHost: toyos\r\nConnection: close\r\n\r\n";
        while (*A && Rlen < (int)sizeof(gHttpReq) - 1) {
            gHttpReq[Rlen++] = *A++;
        }
        P = Path;
        if (*P != '/') {
            gHttpReq[Rlen++] = '/';
        }
        while (*P && Rlen < (int)sizeof(gHttpReq) - 40) {
            gHttpReq[Rlen++] = *P++;
        }
        while (*B && Rlen < (int)sizeof(gHttpReq) - 1) {
            gHttpReq[Rlen++] = *B++;
        }
        gHttpReq[Rlen] = 0;
    }
    if (TcpSend(gHttpReq, (UINTN)Rlen) != 0) {
        TcpClose();
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: tcp send fail\n");
        return STORE_ERR_NET;
    }
    /* 发送后立刻排空 RX，避免首段在进入循环前堆积/丢失 */
    {
        int Warm = 2000;
        while (Warm-- > 0) {
            PollNet();
        }
    }

    Tries = STORE_HTTP_TRIES;
    {
        int Idle = 0;
        int Spin = 0;
        while (Tries-- > 0 && Got < STORE_HTTP_MAX) {
            PollNet();
            if ((++Spin & 0x3FF) == 0) {
                HttpBreath();
            }
            Chunk = 0;
            (void)TcpRecv(Resp + Got, STORE_HTTP_MAX - Got, &Chunk);
            Got += Chunk;
            if (Chunk > 0) {
                Idle = 0;
            } else {
                Idle++;
                if (Idle > 0 && (Idle % STORE_HTTP_IDLE_ACK) == 0) {
                    (void)TcpSendAck(); /* 催促对端重传缺失段 */
                    HttpBreath();
                }
            }
            if (Got >= 16) {
                Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
                if (Rc == -2) {
                    PhysicalMemoryFreePages(Resp, Pages);
                    TcpClose();
                    return STORE_ERR_HTTP;
                }
                if (Rc == 0 && HaveLen && BodyOff + BodyLen <= Got) {
                    break;
                }
                if (Rc == 0 && !HaveLen && TcpPeerClosed() && Idle > STORE_HTTP_IDLE_ACK) {
                    break;
                }
            }
            if (HaveLen && BodyOff + BodyLen <= Got) {
                break;
            }
            /* 无 Content-Length：对端已关且空闲一会儿再结束 */
            if (!HaveLen && TcpPeerClosed() && Chunk == 0 && Got > 0 &&
                Idle > STORE_HTTP_IDLE_ACK * 2) {
                break;
            }
        }
    }
    TcpClose();

ParseHttp:
    Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
    if (Rc != 0 && Got > 0) {
        UINTN i;
        /* 缓冲里找 HTTP/（头被偏移错切时） */
        for (i = 0; i + 5 < Got; i++) {
            if (Resp[i] == 'H' && Resp[i + 1] == 'T' && Resp[i + 2] == 'T' &&
                Resp[i + 3] == 'P' && Resp[i + 4] == '/') {
                Rc = FindBody(Resp + i, Got - i, &BodyOff, &BodyLen, &HaveLen);
                if (Rc == 0) {
                    BodyOff += i;
                    break;
                }
            }
        }
    }
    if (Rc != 0 && Got > 0) {
        /* 仅收到正文：catalog 以 # 开头，ELF 魔数 */
        if (Resp[0] == '#' ||
            (Got >= 4 && Resp[0] == 0x7F && Resp[1] == 'E' && Resp[2] == 'L' &&
             Resp[3] == 'F')) {
            BodyOff = 0;
            BodyLen = Got;
            HaveLen = 1;
            Rc = 0;
            HalConsoleWriteSerial("store: raw body fallback\n");
        }
    }
    if (Rc != 0) {
        HalConsoleWriteSerial("store: bad http got=");
        HalConsoleWriteSerial(Got > 0 ? "nz\n" : "0\n");
        if (Got >= 4) {
            char Snap[8];
            Snap[0] = (char)Resp[0];
            Snap[1] = (char)Resp[1];
            Snap[2] = (char)Resp[2];
            Snap[3] = (char)Resp[3];
            Snap[4] = 0;
            HalConsoleWriteSerial("store: head ");
            HalConsoleWriteSerial(Snap);
            HalConsoleWriteSerial("\n");
        }
        PhysicalMemoryFreePages(Resp, Pages);
        return Rc == -2 ? STORE_ERR_HTTP : STORE_ERR_NET;
    }
    if (HaveLen) {
        if (BodyOff + BodyLen > Got) {
            char Msg[80];
            int n = 0;
            const char *P = "store: truncated got=";
            while (*P) {
                Msg[n++] = *P++;
            }
            /* 十进制长度便于对照 Content-Length */
            {
                UINT32 V = (UINT32)Got;
                char Tmp[12];
                int t = 0;
                if (V == 0) {
                    Tmp[t++] = '0';
                } else {
                    while (V) {
                        Tmp[t++] = (char)('0' + (V % 10));
                        V /= 10;
                    }
                }
                while (t > 0) {
                    Msg[n++] = Tmp[--t];
                }
            }
            Msg[n++] = '/';
            {
                UINT32 V = (UINT32)(BodyOff + BodyLen);
                char Tmp[12];
                int t = 0;
                if (V == 0) {
                    Tmp[t++] = '0';
                } else {
                    while (V) {
                        Tmp[t++] = (char)('0' + (V % 10));
                        V /= 10;
                    }
                }
                while (t > 0) {
                    Msg[n++] = Tmp[--t];
                }
            }
            Msg[n++] = '\n';
            Msg[n] = 0;
            /* Worker 异步：ConsoleNotify 先换行离开 toyos>，勿用裸串口插在提示符后 */
            ConsoleNotify(Msg);
            PhysicalMemoryFreePages(Resp, Pages);
            return STORE_ERR_NET;
        }
    } else {
        BodyLen = Got > BodyOff ? Got - BodyOff : 0;
    }
    if (BodyLen == 0) {
        PhysicalMemoryFreePages(Resp, Pages);
        HalConsoleWriteSerial("store: empty body\n");
        return STORE_ERR_NET;
    }

    {
        UINTN i;
        for (i = 0; i < BodyLen; i++) {
            Resp[i] = Resp[BodyOff + i];
        }
    }
    *OutBody = Resp;
    *OutLen = BodyLen;
    *OutPages = Pages;
    return 0;
}
