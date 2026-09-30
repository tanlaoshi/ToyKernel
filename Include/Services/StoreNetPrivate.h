/*
 * StoreNetPrivate.h — StoreNet 内部共享头（仅 Common/Services/StoreNet 使用）
 *
 * 禁止 User 程序、HAL、Core 包含本文件。
 * 源文件在 Common/Services/StoreNet/（核心 StoreNet.c）。
 * 对外 API 仍在 Store.h。
 */
#ifndef STORE_NET_PRIVATE_H
#define STORE_NET_PRIVATE_H

#include "Store.h"
#include "Tcp.h"
#include "Hal.h"
#include "HalConsole.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "Db.h"

/* ===== 宏（从 StoreNet.c 搬入；值不变） ===== */
#define STORE_HTTP_MAX      (256u * 1024u) /* PR-LAN-store-httpmax：整响应连续页 */
#define STORE_HTTP_TRIES    2000000       /* 紧循环；需覆盖对端 RTO 重传 */
#define STORE_HTTP_IDLE_ACK 4000          /* 无新数据时周期性 dup ACK */
#define STORE_ERR_NET       (-40)
#define STORE_ERR_HTTP      (-41)
#define STORE_ERR_HASH      (-42)
#define STORE_ERR_ALLOC     (-43)
#define STORE_REPO_DEFAULT_IP_QEMU 0x0A000202u /* 10.0.2.2（user-net 宿主） */
#define STORE_REPO_DEFAULT_IP_REAL 0xC0A81F7Cu /* 192.168.31.124（NUC 局域网台式机） */
#define STORE_REPO_DEFAULT_IP      STORE_REPO_DEFAULT_IP_QEMU /* 静态初值；Load 时按环境覆盖 */
#define STORE_REPO_DEFAULT_PORT 8080u

/* 无 store.repo / repo.txt 时的默认 IP：QEMU→网关，真机→LAN 台式机 */
static inline UINT32 StoreRepoDefaultIp(void) {
    return HalCpuIsHypervisor() ? STORE_REPO_DEFAULT_IP_QEMU
                                : STORE_REPO_DEFAULT_IP_REAL;
}

/* ===== StoreNetParse.c ===== */
int FindBody(const UINT8 *Resp, UINTN Len, UINTN *BodyOff, UINTN *BodyLen,
             int *HaveLen);
int HashOk(const char *Expect, const UINT8 *Data, UINTN Len);

/* ===== StoreNetHttp.c ===== */
int HttpGet(UINT32 Ip, UINT16 Port, const char *Path,
            UINT8 **OutBody, UINTN *OutLen, UINT32 *OutPages);
/* ===== StoreNetHttpLwip.c ===== */
int HttpGetLwIp(UINT32 Ip, UINT16 Port, const char *Path,
                UINT8 *Resp, UINTN Cap, UINTN *OutGot);

#endif
