/*
 * StoreNet.c — PR-S2：最小 HTTP/1.0 GET（内建 TCP 客户端）+ store fetch/sync
 *
 * 仓库：ToyDB store.repo=ip:port（默认 10.0.2.2:8080 = QEMU 宿主）
 * 哈希：catalog 的 sha256 字段若为 8 位 hex，按 FNV-1a-32 校验；"-" 跳过。
 */
#include "Store.h"
#include "Tcp.h"
#include "Hal.h"
#include "HalConsole.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "Db.h"

#define STORE_HTTP_MAX     (48u * 1024u) /* 连续页易碎，教学包够用 */
#define STORE_HTTP_TRIES   2000000       /* 紧循环；需覆盖对端 RTO 重传 */
#define STORE_HTTP_IDLE_ACK 4000         /* 无新数据时周期性 dup ACK */
#define STORE_ERR_NET      (-40)
#define STORE_ERR_HTTP     (-41)
#define STORE_ERR_HASH     (-42)
#define STORE_ERR_ALLOC    (-43)
#define STORE_REPO_DEFAULT_IP   0x0A000202u /* 10.0.2.2 */
#define STORE_REPO_DEFAULT_PORT 8080u

static UINT32 gRepoIp = STORE_REPO_DEFAULT_IP;
static UINT16 gRepoPort = (UINT16)STORE_REPO_DEFAULT_PORT;
static char gHttpReq[256]; /* 避免 HttpGet 再占任务栈 */

static void PollNet(void) {
    HalNetPoll();
    TcpPoll();
}

static UINT32 Fnv1a32(const UINT8 *Data, UINTN Len) {
    UINT32 H = 2166136261u;
    UINTN i;

    for (i = 0; i < Len; i++) {
        H ^= Data[i];
        H *= 16777619u;
    }
    return H;
}

static void Hex8(UINT32 V, char Out[9]) {
    static const char *D = "0123456789abcdef";
    int i;

    for (i = 0; i < 8; i++) {
        Out[7 - i] = D[(V >> (i * 4)) & 0xF];
    }
    Out[8] = 0;
}

static int HexNibble(char C) {
    if (C >= '0' && C <= '9') {
        return C - '0';
    }
    if (C >= 'a' && C <= 'f') {
        return C - 'a' + 10;
    }
    if (C >= 'A' && C <= 'F') {
        return C - 'A' + 10;
    }
    return -1;
}

static int HashOk(const char *Expect, const UINT8 *Data, UINTN Len) {
    char Got[9];
    int i;

    if (!Expect || Expect[0] == 0 || Expect[0] == '-') {
        return 1;
    }
    /* 教学：仅认 8 位 FNV-1a-32 hex */
    for (i = 0; i < 8; i++) {
        if (HexNibble(Expect[i]) < 0) {
            return 1; /* 非 8hex 则跳过 */
        }
    }
    if (Expect[8] != 0 && Expect[8] != ' ' && Expect[8] != '|') {
        return 1;
    }
    Hex8(Fnv1a32(Data, Len), Got);
    for (i = 0; i < 8; i++) {
        char A = Expect[i];
        char B = Got[i];
        if (A >= 'A' && A <= 'F') {
            A = (char)(A - 'A' + 'a');
        }
        if (A != B) {
            return 0;
        }
    }
    return 1;
}

static int WaitEstablished(int Tries) {
    while (Tries-- > 0) {
        PollNet();
        if (TcpGetState() == TCP_ESTABLISHED) {
            return 0;
        }
        if (TcpGetState() != TCP_SYN_SENT) {
            return STORE_ERR_NET;
        }
    }
    return STORE_ERR_NET;
}

static int FindBody(const UINT8 *Resp, UINTN Len, UINTN *BodyOff, UINTN *BodyLen,
                    int *HaveLen) {
    UINTN i;
    UINTN Status = 0;
    UINTN ContentLen = (UINTN)-1;
    UINTN HdrEnd = 0;

    if (HaveLen) {
        *HaveLen = 0;
    }
    if (Len < 12) {
        return -1;
    }
    /* HTTP/1.x 200 */
    if (!(Resp[0] == 'H' && Resp[1] == 'T' && Resp[2] == 'T' && Resp[3] == 'P')) {
        return -1;
    }
    for (i = 0; i + 2 < Len && Resp[i] != ' '; i++) {
    }
    if (i + 3 < Len) {
        Status = (UINTN)(Resp[i + 1] - '0') * 100 +
                 (UINTN)(Resp[i + 2] - '0') * 10 +
                 (UINTN)(Resp[i + 3] - '0');
    }
    if (Status != 200) {
        return -2;
    }
    for (i = 0; i + 1 < Len; i++) {
        if (Resp[i] == '\r' && Resp[i + 1] == '\n' &&
            i + 3 < Len && Resp[i + 2] == '\r' && Resp[i + 3] == '\n') {
            HdrEnd = i + 4;
            break;
        }
        if (Resp[i] == '\n' && Resp[i + 1] == '\n') {
            HdrEnd = i + 2;
            break;
        }
    }
    if (HdrEnd == 0) {
        return -1;
    }
    /* Content-Length（可选，大小写不敏感） */
    for (i = 0; i + 15 < HdrEnd; i++) {
        const char *Key = "content-length:";
        int Match = 1;
        int k;

        for (k = 0; Key[k]; k++) {
            char C = (char)Resp[i + (UINTN)k];
            if (C >= 'A' && C <= 'Z') {
                C = (char)(C - 'A' + 'a');
            }
            if (C != Key[k]) {
                Match = 0;
                break;
            }
        }
        if (Match) {
            UINTN j = i + 15;
            while (j < HdrEnd && (Resp[j] == ' ' || Resp[j] == '\t')) {
                j++;
            }
            ContentLen = 0;
            while (j < HdrEnd && Resp[j] >= '0' && Resp[j] <= '9') {
                ContentLen = ContentLen * 10 + (UINTN)(Resp[j] - '0');
                j++;
            }
            if (HaveLen) {
                *HaveLen = 1;
            }
            break;
        }
    }
    *BodyOff = HdrEnd;
    if (ContentLen != (UINTN)-1) {
        *BodyLen = ContentLen;
    } else {
        *BodyLen = Len > HdrEnd ? Len - HdrEnd : 0;
    }
    return 0;
}

/*
 * HTTP/1.0 GET path → *OutBody 指向堆页内正文（调用方 FreePages）。
 * 成功 0；STORE_ERR_* / -2 兼容旧调用（改用 STORE_ERR_HTTP）。
 */
static int HttpGet(UINT32 Ip, UINT16 Port, const char *Path,
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
        while (Tries-- > 0 && Got < STORE_HTTP_MAX) {
            PollNet();
            Chunk = 0;
            (void)TcpRecv(Resp + Got, STORE_HTTP_MAX - Got, &Chunk);
            Got += Chunk;
            if (Chunk > 0) {
                Idle = 0;
            } else {
                Idle++;
                if (Idle > 0 && (Idle % STORE_HTTP_IDLE_ACK) == 0) {
                    (void)TcpSendAck(); /* 催促对端重传缺失段 */
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
            HalConsoleWriteSerial(Msg);
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

static int EnsureStoreDir(void) {
    int Err = FileSystemMakeDirectory("Store");
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

static int ParseRepoVal(const char *Val) {
    UINT32 Ip;
    UINT32 Port = STORE_REPO_DEFAULT_PORT;
    const char *Colon;

    if (!Val || !Val[0]) {
        return -1;
    }
    Colon = Val;
    while (*Colon && *Colon != ':') {
        Colon++;
    }
    if (*Colon == ':') {
        char IpBuf[32];
        int n = 0;
        const char *S = Val;
        while (S < Colon && n + 1 < (int)sizeof(IpBuf)) {
            IpBuf[n++] = *S++;
        }
        IpBuf[n] = 0;
        if (HalNetParseIp(IpBuf, &Ip) != 0) {
            return -1;
        }
        Port = 0;
        Colon++;
        while (*Colon >= '0' && *Colon <= '9') {
            Port = Port * 10 + (UINT32)(*Colon - '0');
            Colon++;
        }
        if (Port == 0 || Port > 65535) {
            return -1;
        }
    } else {
        if (HalNetParseIp(Val, &Ip) != 0) {
            return -1;
        }
    }
    gRepoIp = Ip;
    gRepoPort = (UINT16)Port;
    return 0;
}

void StoreRepoLoadFromDb(void) {
    char Val[DB_VAL_MAX];

    gRepoIp = STORE_REPO_DEFAULT_IP;
    gRepoPort = (UINT16)STORE_REPO_DEFAULT_PORT;
    if (DbGet("store.repo", Val, sizeof(Val)) == DB_OK) {
        (void)ParseRepoVal(Val);
    }
}

int StoreRepoSet(const char *IpPort) {
    char Val[64];
    int i = 0;

    if (ParseRepoVal(IpPort) != 0) {
        return -1;
    }
    while (IpPort[i] && i + 1 < (int)sizeof(Val)) {
        Val[i] = IpPort[i];
        i++;
    }
    Val[i] = 0;
    if (DbSet("store.repo", Val) != DB_OK) {
        return -1;
    }
    return 0;
}

void StoreRepoGet(UINT32 *OutIp, UINT16 *OutPort) {
    StoreRepoLoadFromDb();
    if (OutIp) {
        *OutIp = gRepoIp;
    }
    if (OutPort) {
        *OutPort = gRepoPort;
    }
}

int StoreFetchPath(const char *UrlPath, const char *DestRel,
                   const char *ExpectHash) {
    UINT8 *Body = 0;
    UINTN Len = 0;
    UINT32 Pages = 0;
    int Rc;
    int Err;

    StoreRepoLoadFromDb();
    Rc = HttpGet(gRepoIp, gRepoPort, UrlPath, &Body, &Len, &Pages);
    if (Rc != 0) {
        return Rc;
    }
    if (!HashOk(ExpectHash, Body, Len)) {
        PhysicalMemoryFreePages(Body, Pages);
        HalConsoleWriteSerial("store: hash mismatch\n");
        return STORE_ERR_HASH;
    }
    Err = EnsureStoreDir();
    if (Err != FAT_OK) {
        PhysicalMemoryFreePages(Body, Pages);
        return Err;
    }
    Err = FileSystemWriteFile(DestRel, Body, Len);
    PhysicalMemoryFreePages(Body, Pages);
    return Err == FAT_OK ? 0 : Err;
}

int StoreSyncCatalog(void) {
    return StoreFetchPath("/catalog.txt", "Store/catalog.txt", "-");
}

static int IdEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

int StoreFetchId(const char *Id) {
    STORE_ENTRY *Tab = StoreScratchTab();
    int Count = 0;
    int i;
    int Err;
    char Url[96];
    char Dest[96];
    int n;
    int j;

    if (!Id || !Id[0]) {
        return FAT_ERR_INVAL;
    }
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count == 0) {
        return Err < 0 ? Err : FAT_ERR_NOENT;
    }
    for (i = 0; i < Count; i++) {
        if (!IdEq(Tab[i].Id, Id)) {
            continue;
        }
        n = 0;
        Url[n++] = '/';
        j = 0;
        while (Tab[i].File[j] && n + 1 < (int)sizeof(Url)) {
            Url[n++] = Tab[i].File[j++];
        }
        Url[n] = 0;
        n = 0;
        {
            const char *P = "Store/";
            while (*P && n + 1 < (int)sizeof(Dest)) {
                Dest[n++] = *P++;
            }
        }
        j = 0;
        while (Tab[i].File[j] && n + 1 < (int)sizeof(Dest)) {
            Dest[n++] = Tab[i].File[j++];
        }
        Dest[n] = 0;
        return StoreFetchPath(Url, Dest, Tab[i].Sha256);
    }
    return FAT_ERR_NOENT;
}
