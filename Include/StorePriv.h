/*
 * StorePriv.h — Store 内部（仅 Common/Services/Store）
 *
 * 对外 API 仍在 Store.h。User 勿 include。
 */
#ifndef STORE_PRIV_H
#define STORE_PRIV_H

#include "Store.h"

void NormalizeDepends(char *Dep);

#endif
