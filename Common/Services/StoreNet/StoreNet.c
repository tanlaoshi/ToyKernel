/*
 * StoreNet.c — 仓库地址与 fetch/sync
 * HTTP：StoreNetHttp.c；解析与哈希：StoreNetParse.c。
 */
#include "StoreNetPriv.h"

static UINT32 gRepoIp = STORE_REPO_DEFAULT_IP;
static UINT16 gRepoPort = (UINT16)STORE_REPO_DEFAULT_PORT;

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
