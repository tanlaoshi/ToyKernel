/*
 * StoreNetPrivate.h — StoreNet 内部共享头（仅 Common/Services/StoreNet 使用）
 *
 * 禁止 User 程序、HAL、Common/Core 包含本文件。
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
#define STORE_HTTP_MAX      (48u * 1024u) /* 连续页易碎，教学包够用 */
#define STORE_HTTP_TRIES    2000000       /* 紧循环；需覆盖对端 RTO 重传 */
#define STORE_HTTP_IDLE_ACK 4000          /* 无新数据时周期性 dup ACK */
#define STORE_ERR_NET       (-40)
#define STORE_ERR_HTTP      (-41)
#define STORE_ERR_HASH      (-42)
#define STORE_ERR_ALLOC     (-43)
#define STORE_REPO_DEFAULT_IP   0x0A000202u /* 10.0.2.2 */
#define STORE_REPO_DEFAULT_PORT 8080u

/* ===== StoreNetParse.c ===== */
int FindBody(const UINT8 *Resp, UINTN Len, UINTN *BodyOff, UINTN *BodyLen,
             int *HaveLen);
int HashOk(const char *Expect, const UINT8 *Data, UINTN Len);

/* ===== StoreNetHttp.c ===== */
int HttpGet(UINT32 Ip, UINT16 Port, const char *Path,
            UINT8 **OutBody, UINTN *OutLen, UINT32 *OutPages);

#endif
