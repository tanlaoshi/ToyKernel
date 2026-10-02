/*
 * StoreNet.c — 仓库地址与 sync（目录）/ fetch（兼容→install）
 * HTTP：StoreNetHttp.c；解析与哈希：StoreNetParse.c。
 */
#include "StoreNetPrivate.h"

static UINT32 gRepoIp = STORE_REPO_DEFAULT_IP;
static UINT16 gRepoPort = (UINT16)STORE_REPO_DEFAULT_PORT;

static int EnsureStoreDir(void) {
    int Err = FileSystemMakeDirectory(STORE_DIR);
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

    gRepoIp = StoreRepoDefaultIp();
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
    /* DestRel 可能是 Store/remote.cat，也可能是 Apps/…；建 Store/ 无害 */
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
    /* 只更新远程目录表；不批量拉 ELF */
    return StoreFetchPath("/catalog.txt", STORE_REMOTE_CAT, "-");
}

int StoreFetchId(const char *Id) {
    /* 兼容旧子命令：等价于 install（HTTP 直达 Apps，无 Cache） */
    return StoreInstall(Id);
}
