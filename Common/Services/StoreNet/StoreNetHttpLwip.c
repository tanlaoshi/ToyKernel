/*
 * StoreNetHttpLwip.c — HTTP/1.0 GET via lwIP socket（lwip on 后必走此路）
 * 自研 Tcp 在 gLwIpRx=1 时 NetSendIp 直接失败 → 旧 HttpGet 会 tcp syn fail。
 */
#include "StoreNetPrivate.h"
#include "LwIp.h"
#include "Scheduler.h"
#include "Console.h"

static int BuildGetReq(const char *Path, char *Out, int OutMax) {
    const char *A = "GET ";
    const char *B = " HTTP/1.0\r\nHost: toyos\r\nConnection: close\r\n\r\n";
    const char *P;
    int N = 0;

    while (*A && N < OutMax - 1) {
        Out[N++] = *A++;
    }
    P = Path;
    if (*P != '/') {
        Out[N++] = '/';
    }
    while (*P && N < OutMax - 40) {
        Out[N++] = *P++;
    }
    while (*B && N < OutMax - 1) {
        Out[N++] = *B++;
    }
    Out[N] = 0;
    return N;
}

/*
 * 成功：*OutGot 为完整 HTTP 响应字节数。
 * 失败：STORE_ERR_*（调用方释放 Resp）。
 */
int HttpGetLwIp(UINT32 Ip, UINT16 Port, const char *Path,
                UINT8 *Resp, UINTN Cap, UINTN *OutGot) {
    int Sock;
    char Req[256];
    int Rlen;
    UINTN Got = 0;
    int Tries;
    int Idle = 0;
    int N;
    int Spin = 0;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    int Rc;

    if (!Path || !Resp || !OutGot || Cap == 0) {
        return STORE_ERR_NET;
    }
    *OutGot = 0;

    if (!LwIpActive() && LwIpInit() != 0) {
        ConsoleNotify("store: lwip init fail\n");
        return STORE_ERR_NET;
    }

    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        ConsoleNotify("store: lwip socket fail\n");
        return STORE_ERR_NET;
    }
    if (LwIpSocketConnect(Sock, Ip, Port) != 0) {
        LwIpSocketClose(Sock);
        ConsoleNotify("store: tcp syn fail\n");
        return STORE_ERR_NET;
    }

    Rlen = BuildGetReq(Path, Req, (int)sizeof(Req));
    if (Rlen <= 0 || LwIpSocketSend(Sock, Req, (UINTN)Rlen) < 0) {
        LwIpSocketClose(Sock);
        ConsoleNotify("store: tcp send fail\n");
        return STORE_ERR_NET;
    }

    /*
     * 收满 Content-Length 或 EOF。
     * 有 CL 未齐：允许短空闲等窗推进；长时间无字节则停（避免 Tries 近永久忙）。
     */
    Tries = 60000;
    while (Tries-- > 0 && Got < Cap) {
        N = LwIpSocketRecv(Sock, Resp + Got, Cap - Got, 8);
        if (N > 0) {
            Got += (UINTN)N;
            Idle = 0;
            if (Got >= 16) {
                Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
                if (Rc == -2) {
                    LwIpSocketClose(Sock);
                    return STORE_ERR_HTTP;
                }
                if (Rc == 0 && HaveLen && BodyOff + BodyLen <= Got) {
                    break;
                }
            }
            continue;
        }
        if (N == -2) {
            break;
        }
        if (N < 0) {
            break;
        }
        Idle++;
        LwIpService();
        if ((++Spin & 0x3F) == 0) {
            SchedulerIoBreath();
        }
        if (HaveLen && BodyOff + BodyLen <= Got) {
            break;
        }
        /* 无 CL：有数据后空闲则停 */
        if (!HaveLen && Got > 0 && Idle > 1000) {
            break;
        }
        /* 有 CL 未齐：~数秒无新字节 → 交给上层报 truncated */
        if (HaveLen && Got > 0 && Idle > 4000) {
            break;
        }
        if (Got == 0 && Idle > 3000) {
            break;
        }
    }
    LwIpSocketClose(Sock);
    if (Got == 0) {
        ConsoleNotify("store: tcp connect timeout\n");
        return STORE_ERR_NET;
    }
    *OutGot = Got;
    return 0;
}
