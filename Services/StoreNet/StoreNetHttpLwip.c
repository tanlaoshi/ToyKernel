/*
 * StoreNetHttpLwip.c — HTTP/1.0 GET via lwIP socket（lwip on 后必走此路）
 * 自研 Tcp 在 gLwIpRx=1 时 NetSendIp 直接失败 → 旧 HttpGet 会 tcp syn fail。
 */
#include "StoreNetPrivate.h"
#include "LwIp.h"
#include "Scheduler.h"
#include "Console.h"
#include "HalConsole.h"

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

static void LogGot(const char *Tag, UINTN Got) {
    char Msg[48];
    int n = 0;
    UINT32 V;
    char Tmp[12];
    int t;

    while (*Tag && n < 28) {
        Msg[n++] = *Tag++;
    }
    V = (UINT32)Got;
    t = 0;
    if (V == 0) {
        Tmp[t++] = '0';
    } else {
        while (V) {
            Tmp[t++] = (char)('0' + (V % 10));
            V /= 10;
        }
    }
    while (t > 0 && n < 46) {
        Msg[n++] = Tmp[--t];
    }
    Msg[n++] = '\n';
    Msg[n] = 0;
    HalConsoleWriteSerial(Msg);
}

/*
 * 单次 GET。成功：*OutGot 为完整 HTTP 响应字节数。
 * 失败：STORE_ERR_*（调用方释放 Resp）。
 */
static int HttpGetLwIpOnce(UINT32 Ip, UINT16 Port, const char *Path,
                           UINT8 *Resp, UINTN Cap, UINTN *OutGot) {
    int Sock;
    char Req[256];
    int Rlen;
    UINTN Got = 0;
    int Tries;
    int Idle = 0;
    int N;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;
    int Rc;
    UINTN LastLog = 0;

    *OutGot = 0;

    HalConsoleWriteSerial("store: http begin\n");
    {
        char IpBuf[20];
        HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
        HalConsoleWriteSerial("store: http repo ");
        HalConsoleWriteSerial(IpBuf);
        HalConsoleWriteSerial("\n");
    }
    Sock = LwIpSocketCreate();
    if (Sock < 0) {
        HalConsoleWriteSerial("store: lwip socket fail\n");
        return STORE_ERR_NET;
    }
    HalConsoleWriteSerial("store: http connect\n");
    if (LwIpSocketConnect(Sock, Ip, Port) != 0) {
        LwIpSocketClose(Sock);
        HalConsoleWriteSerial("store: tcp syn fail\n");
        return STORE_ERR_NET;
    }

    Rlen = BuildGetReq(Path, Req, (int)sizeof(Req));
    if (Rlen <= 0 || LwIpSocketSend(Sock, Req, (UINTN)Rlen) < 0) {
        LwIpSocketClose(Sock);
        HalConsoleWriteSerial("store: tcp send fail\n");
        return STORE_ERR_NET;
    }
    HalConsoleWriteSerial("store: http sent\n");

    /*
     * 非阻塞收 + 每圈 Breath：Worker 上阻塞 Recv/Halt 会鼠标假死。
     * Idle 约数秒无字节则放弃（宿主已 200 仍 empty → 上层 retry）。
     */
    Tries = 20000;
    while (Tries-- > 0 && Got < Cap) {
        N = LwIpSocketRecv(Sock, Resp + Got, Cap - Got, -1);
        if (N > 0) {
            Got += (UINTN)N;
            Idle = 0;
            if (Got - LastLog >= 64 || Got == (UINTN)N) {
                LogGot("store: http got=", Got);
                LastLog = Got;
            }
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
            SchedulerIoBreath();
            continue;
        }
        if (N == -2) {
            HalConsoleWriteSerial(Got == 0 ? "store: http eof0\n" : "store: http eof\n");
            break;
        }
        if (N < 0) {
            HalConsoleWriteSerial("store: http rst0\n");
            break;
        }
        Idle++;
        LwIpService();
        SchedulerIoBreath();
        if (HaveLen && BodyOff + BodyLen <= Got) {
            break;
        }
        if (!HaveLen && Got > 0 && Idle > 2000) {
            break;
        }
        if (HaveLen && BodyOff + BodyLen > Got && Idle > 4000) {
            break;
        }
        if (Got == 0 && Idle > 2500) {
            break;
        }
    }
    LwIpSocketClose(Sock);
    if (Got == 0) {
        HalConsoleWriteSerial("store: http empty\n");
        return STORE_ERR_NET;
    }
    LogGot("store: http end=", Got);
    *OutGot = Got;
    return 0;
}

/*
 * 成功：*OutGot 为完整 HTTP 响应字节数。
 * 失败：STORE_ERR_*（调用方释放 Resp）。
 * 头到齐、体未到时最多再试 2 次，避免 UI 卡 sync: catalog。
 */
int HttpGetLwIp(UINT32 Ip, UINT16 Port, const char *Path,
                UINT8 *Resp, UINTN Cap, UINTN *OutGot) {
    int Attempt;
    int Rc;
    UINTN Got = 0;
    UINTN BodyOff = 0;
    UINTN BodyLen = 0;
    int HaveLen = 0;

    if (!Path || !Resp || !OutGot || Cap == 0) {
        return STORE_ERR_NET;
    }
    *OutGot = 0;

    if (!LwIpActive() && LwIpInitialize() != 0) {
        HalConsoleWriteSerial("store: lwip init fail\n");
        return STORE_ERR_NET;
    }

    for (Attempt = 0; Attempt < 3; Attempt++) {
        Got = 0;
        Rc = HttpGetLwIpOnce(Ip, Port, Path, Resp, Cap, &Got);
        if (Rc != 0) {
            if (Attempt + 1 < 3) {
                HalConsoleWriteSerial("store: http retry\n");
                continue;
            }
            return Rc;
        }
        BodyOff = 0;
        BodyLen = 0;
        HaveLen = 0;
        Rc = FindBody(Resp, Got, &BodyOff, &BodyLen, &HaveLen);
        if (Rc == 0 && HaveLen && BodyOff + BodyLen > Got) {
            if (Attempt + 1 < 3) {
                HalConsoleWriteSerial("store: http retry\n");
                continue;
            }
            *OutGot = Got;
            return 0;
        }
        *OutGot = Got;
        return 0;
    }
    *OutGot = Got;
    return STORE_ERR_NET;
}
